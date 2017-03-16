#include "revtracer.h"

#include "execenv.h"
#include "callgates.h"

#include <intrin.h>


void InitSymbolicHandler(ExecutionEnvironment *pEnv);
void *CreateSymbolicVariable(const char *name);

namespace rev {

	/* Kernel32.dll replacements *********************************************************/

	typedef void *LPVOID, *PVOID;
	typedef long NTSTATUS;
	typedef void *HANDLE;
	typedef int BOOL;
	typedef const void *LPCVOID;
	typedef nodep::DWORD *LPDWORD;

	typedef unsigned long ULONG;

#define TRUE  1
#define FALSE 0

#define PAGE_EXECUTE           0x10     
#define PAGE_EXECUTE_READ      0x20     
#define PAGE_EXECUTE_READWRITE 0x40     
#define PAGE_EXECUTE_WRITECOPY 0x80     

#define MEM_COMMIT                  0x1000      
#define MEM_RESERVE                 0x2000      

	typedef void* (*AllocateMemoryCall)(size_t size);

	HANDLE Kernel32GetCurrentThread() {
		return (HANDLE)0xFFFFFFFE;
	}

	typedef struct {
		nodep::WORD    LimitLow;
		nodep::WORD    BaseLow;
		union {
			struct {
				nodep::BYTE    BaseMid;
				nodep::BYTE    Flags1;     // Declare as bytes to avoid alignment
				nodep::BYTE    Flags2;     // Problems.
				nodep::BYTE    BaseHi;
			} Bytes;
			struct {
				nodep::DWORD   BaseMid : 8;
				nodep::DWORD   Type : 5;
				nodep::DWORD   Dpl : 2;
				nodep::DWORD   Pres : 1;
				nodep::DWORD   LimitHi : 4;
				nodep::DWORD   Sys : 1;
				nodep::DWORD   Reserved_0 : 1;
				nodep::DWORD   Default_Big : 1;
				nodep::DWORD   Granularity : 1;
				nodep::DWORD   BaseHi : 8;
			} Bits;
		} HighWord;
	} LDT_ENTRY, *LPLDT_ENTRY;

	typedef nodep::DWORD THREADINFOCLASS;



	/* Default API functions ************************************************************/

	void NoDbgPrint(const unsigned int printMask, const char *fmt, ...) { }

	void DefaultIpcInitialize() {
	}

	void *DefaultMemoryAlloc(unsigned long dwSize) {
		return ((AllocateMemoryCall)revtracerAPI.lowLevel.ntAllocateVirtualMemory)(dwSize);
	}

	void DefaultMemoryFree(void *ptr) {
		//TODO: implement VirtualFree
	}

	nodep::QWORD DefaultTakeSnapshot() {
		return 0;
	}

	nodep::QWORD DefaultRestoreSnapshot() {
		return 0;
	}

	void DefaultInitializeContextFunc(void *context) { }
	void DefaultCleanupContextFunc(void *context) { }

	nodep::DWORD DefaultExecutionBeginFunc(void *context, ADDR_TYPE nextInstruction, void *cbCtx) {
		return EXECUTION_ADVANCE;
	}

	nodep::DWORD DefaultExecutionControlFunc(void *context, ADDR_TYPE nextInstruction, void *cbCtx) {
		return EXECUTION_ADVANCE;
	}

	nodep::DWORD DefaultExecutionEndFunc(void *context, void *cbCtx) {
		return EXECUTION_TERMINATE;
	}

	nodep::DWORD DefaultBranchHandlerFunc(void *context, void *userContext, ADDR_TYPE nextInstruction) {
		return EXECUTION_ADVANCE;
	}

	void DefaultSyscallControlFunc(void *context, void *userContext) { }

	void DefaultTrackCallback(nodep::DWORD value, nodep::DWORD address, nodep::DWORD segSel) { }
	void DefaultMarkCallback(nodep::DWORD oldValue, nodep::DWORD newValue, nodep::DWORD address, nodep::DWORD segSel) { }

	void DefaultNtAllocateVirtualMemory() {
		DEBUG_BREAK;
	}

	void DefaultNtFreeVirtualMemory() {
		DEBUG_BREAK;
	}

	void DefaultNtQueryInformationThread() {
		DEBUG_BREAK;
	}

	void DefaultRtlNtStatusToDosError() {
		DEBUG_BREAK;
	}

	void Defaultvsnprintf_s() {
		DEBUG_BREAK;
	}

	void __stdcall DefaultSymbolicHandler(void *context, void *offset, void *address) {
		return;
	}

	/* Execution context callbacks ********************************************************/
	void GetCurrentRegisters(void *ctx, ExecutionRegs *regs) {
		struct ExecutionEnvironment *pCtx = (struct ExecutionEnvironment *)ctx;
		rev_memcpy(regs, (struct ExecutionEnvironment *)pCtx->runtimeContext.registers, sizeof(*regs));
		regs->esp = pCtx->runtimeContext.virtualStack;
	}

	void *GetMemoryInfo(void *ctx, ADDR_TYPE addr) {
		struct ExecutionEnvironment *pEnv = (struct ExecutionEnvironment *)ctx;
		nodep::DWORD ret = pEnv->ac.Get((nodep::DWORD)addr/* + revtracerConfig.segmentOffsets[segSel & 0xFFFF]*/);
		return (void *)ret;
	}

	bool GetLastBasicBlockInfo(void *ctx, BasicBlockInfo *info) {
		struct ExecutionEnvironment *pEnv = (struct ExecutionEnvironment *)ctx;

		RiverBasicBlock *pCB = pEnv->blockCache.FindBlock(pEnv->lastFwBlock);
		if (nullptr != pCB) {
			info->address = (rev::ADDR_TYPE)pCB->address;
			info->cost = pCB->dwOrigOpCount;
		}
		return false;
	}


	/* Inproc API *************************************************************************/

	RevtracerAPI revtracerAPI = {
		NoDbgPrint,

		DefaultMemoryAlloc,
		DefaultMemoryFree,

		DefaultTakeSnapshot,
		DefaultRestoreSnapshot,

		DefaultInitializeContextFunc,
		DefaultCleanupContextFunc,
		
		DefaultBranchHandlerFunc,
		DefaultSyscallControlFunc,

		DefaultIpcInitialize,

		DefaultTrackCallback,
		DefaultMarkCallback,

		DefaultSymbolicHandler,

		{
			(ADDR_TYPE)DefaultNtAllocateVirtualMemory,
			(ADDR_TYPE)DefaultNtFreeVirtualMemory,

			(ADDR_TYPE)DefaultNtQueryInformationThread,
			(ADDR_TYPE)DefaultRtlNtStatusToDosError,

			(ADDR_TYPE)Defaultvsnprintf_s
		}
	};

	RevtracerConfig revtracerConfig = {
		0,
		0
	};

	nodep::DWORD miniStack[4096];
	nodep::DWORD shadowStack = (nodep::DWORD)&(miniStack[4090]);

	struct ExecutionEnvironment *pEnv = NULL;

	void CreateHook(ADDR_TYPE orig, ADDR_TYPE det) {
		RiverBasicBlock *pBlock = pEnv->blockCache.NewBlock((nodep::UINT_PTR)orig);
		pBlock->address = (nodep::DWORD)det;
		pEnv->codeGen.Translate(pBlock, revtracerConfig.featureFlags);
		pBlock->address = (nodep::DWORD)orig;
		pBlock->dwFlags |= RIVER_BASIC_BLOCK_DETOUR;

		revtracerAPI.dbgPrintFunc(PRINT_INFO | PRINT_CONTAINER, "Added detour from 0x%08x to 0x%08x\n", orig, det);
	}

#ifdef _MSC_VER
#define ADDR_OF_RET_ADDR _AddressOfReturnAddress
#else
#define ADDR_OF_RET_ADDR() ({ int addr; __asm__ ("lea 4(%%ebp), %0" : : "r" (addr)); addr; })
#endif

	void TracerInitialization() { // parameter is not initialized (only used to get the 
		nodep::UINT_PTR rgs = (nodep::UINT_PTR)ADDR_OF_RET_ADDR() + sizeof(void *);
		
		Initialize();

		pEnv->runtimeContext.registers = rgs;

		revtracerAPI.dbgPrintFunc(PRINT_INFO | PRINT_CONTAINER, "Entry point @%08x\n", (nodep::DWORD)revtracerConfig.entryPoint);
		RiverBasicBlock *pBlock = pEnv->blockCache.NewBlock((nodep::UINT_PTR)revtracerConfig.entryPoint);
		pBlock->address = (nodep::DWORD)revtracerConfig.entryPoint;
		pEnv->codeGen.Translate(pBlock, revtracerConfig.featureFlags);

		revtracerAPI.dbgPrintFunc(PRINT_INFO | PRINT_CONTAINER, "New entry point @%08x\n", (nodep::DWORD)pBlock->pFwCode);
		
		// TODO: replace with address of the actual terminate process
		pEnv->exitAddr = (nodep::DWORD)revtracerAPI.lowLevel.ntTerminateProcess;

		/*pEnv->runtimeContext.execBuff -= 4;
		*((DWORD *)pEnv->runtimeContext.execBuff) = (DWORD)revtracerConfig.entryPoint;*/
		
		//switch (revtracerAPI.executionBegin(pEnv->userContext, revtracerConfig.entryPoint, pEnv)) {
		switch (revtracerAPI.branchHandler(pEnv, pEnv->userContext, revtracerConfig.entryPoint)) {
			case EXECUTION_ADVANCE :
				revtracerAPI.dbgPrintFunc(PRINT_INFO | PRINT_CONTAINER, "%d detours needed.\n", revtracerConfig.hookCount);
				for (nodep::DWORD i = 0; i < revtracerConfig.hookCount; ++i) {
					CreateHook(revtracerConfig.hooks[i].originalAddr, revtracerConfig.hooks[i].detourAddr);
				}
				pEnv->lastFwBlock = (nodep::UINT_PTR)revtracerConfig.entryPoint;
				pEnv->bForward = 1;
				pBlock->MarkForward();

				revtracerAPI.dbgPrintFunc(PRINT_INFO | PRINT_CONTAINER, "Translated entry point %08x.\n", pBlock->pFwCode);
				revtracerConfig.entryPoint = pBlock->pFwCode;
				break;
			case EXECUTION_TERMINATE :
				revtracerConfig.entryPoint = revtracerAPI.lowLevel.ntTerminateProcess;
				break;
			case EXECUTION_BACKTRACK :
				revtracerAPI.dbgPrintFunc(PRINT_INFO | PRINT_CONTAINER, "EXECUTION_BACKTRACK @executionBegin");
				revtracerConfig.entryPoint = revtracerAPI.lowLevel.ntTerminateProcess;
				break;
		}
	}

	NAKED  void RevtracerPerform() {
#ifdef _MSC_VER
		__asm {
			xchg esp, shadowStack;
			pushad;
			pushfd;
			call TracerInitialization;
			popfd;
			popad;
			xchg esp, shadowStack;

			jmp dword ptr[revtracerConfig.entryPoint];
		}
#else
		__asm__ (
				"xchgl %0, %%esp             \n\t"
				"pushal                      \n\t"
				"pushfl                      \n\t"
				"call %P1                    \n\t"
				"popfl                       \n\t"
				"popal                       \n\t"
				"xchgl %0, %%esp" : : "m" (shadowStack), "i" (TracerInitialization)
				);
		__asm__ (
				"jmp *%0" : : "m" (revtracerConfig.entryPoint)
				);
#endif
	}




	/* Segment initialization *************************************************************/

	//DWORD segmentOffsets[0x100];
	/*void InitSegment(DWORD dwSeg) {
		LDT_ENTRY entry;
		Kernel32GetThreadSelectorEntry(Kernel32GetCurrentThread(), dwSeg, &entry);

		DWORD base = entry.BaseLow | (entry.HighWord.Bytes.BaseMid << 16) | (entry.HighWord.Bytes.BaseHi << 24);
		DWORD limit = entry.LimitLow | (entry.HighWord.Bits.LimitHi << 16);

		if (entry.HighWord.Bits.Granularity) {
			limit = (limit << 12) | 0x0FFF;
		}

		segmentOffsets[dwSeg] = base;
	}


	void InitSegments() {
		for (DWORD i = 0; i < 0x100; ++i) {
			InitSegment(i);
		}
	}*/

	/* DLL API ****************************************************************************/


	void SetDbgPrint(DbgPrintFunc func) {
		revtracerAPI.dbgPrintFunc = func;
	}

	void SetMemoryMgmt(MemoryAllocFunc alc, MemoryFreeFunc fre) {
		revtracerAPI.memoryAllocFunc = alc;
		revtracerAPI.memoryFreeFunc = fre;
	}

	void SetSnapshotMgmt(TakeSnapshotFunc ts, RestoreSnapshotFunc rs) {
		revtracerAPI.takeSnapshot = ts;
		revtracerAPI.restoreSnapshot = rs;
	}

	void SetLowLevelAPI(LowLevelRevtracerAPI *llApi) {
		revtracerAPI.lowLevel.ntAllocateVirtualMemory = llApi->ntAllocateVirtualMemory;
		revtracerAPI.lowLevel.ntFreeVirtualMemory = llApi->ntFreeVirtualMemory;
		revtracerAPI.lowLevel.ntQueryInformationThread = llApi->ntQueryInformationThread;

		revtracerAPI.lowLevel.rtlNtStatusToDosError = llApi->rtlNtStatusToDosError;

		revtracerAPI.lowLevel.vsnprintf_s = llApi->vsnprintf_s;
	}

	void SetContextMgmt(InitializeContextFunc initCtx, CleanupContextFunc cleanCtx) {
		revtracerAPI.initializeContext = initCtx;
		revtracerAPI.cleanupContext = cleanCtx;
	}

	//void SetControlMgmt(ExecutionControlFunc execCtl, SyscallControlFunc syscallCtl) {
	void SetControlMgmt(BranchHandlerFunc branchCtl, SyscallControlFunc syscallCtl) {
		revtracerAPI.branchHandler = branchCtl;
		revtracerAPI.syscallControl = syscallCtl;
	}

	void SetContext(ADDR_TYPE ctx) {
		revtracerConfig.context = ctx;
	}

	void SetEntryPoint(ADDR_TYPE ep) {
		revtracerConfig.entryPoint = ep;
	}

	void Initialize() {
		revtracerAPI.ipcLibInitialize();

		revtracerAPI.dbgPrintFunc(PRINT_INFO | PRINT_CONTAINER, "Feature flags %08x, entrypoint %08x\n", revtracerConfig.featureFlags, revtracerConfig.entryPoint);

		pEnv = new ExecutionEnvironment(revtracerConfig.featureFlags, 0x1000000, 0x10000, 0x4000000, 0x4000000, 16, 0x10000);
		pEnv->userContext = revtracerConfig.context; //AllocUserContext(pEnv, revtracerConfig.contextSize);

		revtracerConfig.pRuntime = &pEnv->runtimeContext;
	}

	void Execute(int argc, char *argv[]) {
		nodep::DWORD ret;
		//if (EXECUTION_ADVANCE == revtracerAPI.executionBegin(pEnv->userContext, revtracerConfig.entryPoint, pEnv)) {
		if (EXECUTION_ADVANCE == revtracerAPI.branchHandler(pEnv, pEnv->userContext, revtracerConfig.entryPoint)) {
			ret = call_cdecl_2(pEnv, (_fn_cdecl_2)revtracerConfig.entryPoint, (void *)argc, (void *)argv);
			revtracerAPI.dbgPrintFunc(PRINT_INFO | PRINT_CONTAINER, "Done. ret = %d\n\n", ret);
		}
	}

	nodep::DWORD __stdcall MarkAddr(void *pEnv, nodep::DWORD dwAddr, nodep::DWORD value, nodep::DWORD segSel);
	void MarkMemoryValue(void *ctx, ADDR_TYPE addr, nodep::DWORD value) {
		MarkAddr((ExecutionEnvironment *)ctx, (nodep::DWORD)addr, value, 0x2B);
	}

};
