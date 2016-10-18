#ifdef _WIN32
#include <Windows.h>
#elif defined(__linux__)
#include <stdlib.h>
#include <sys/mman.h>
#include <string.h>
#include <dlfcn.h>
#endif

#include "Inproc.Mapper.h"

namespace ldr {
#ifdef _WIN32

// virtual memory functions
	static const DWORD PageProtections[8] = {
		PAGE_NOACCESS,				// 0 ---
		PAGE_EXECUTE,				// 1 --X
		PAGE_READWRITE,				// 2 -W- (specified as RW)
		PAGE_EXECUTE_READWRITE,		// 3 -WX (specified as RWX)
		PAGE_READONLY,				// 4 R--
		PAGE_EXECUTE_READ,			// 5 R-X
		PAGE_READWRITE,				// 6 RW-
		PAGE_EXECUTE_READWRITE		// 7 RWX
	};
#define VIRTUAL_ALLOC(addr, size, protect) VirtualAlloc((addr), (size), MEM_RESERVE | MEM_COMMIT, (protect))
#define VIRTUAL_PROTECT(addr, size, newProtect, oldProtect) (TRUE == VirtualProtect((addr), (size), (newProtect), &(oldProtect)))

// module functions
#define MODULE_PTR HMODULE
#define GET_MODULE_HANDLE(name) GetModuleHandleA((name))
#define GET_PROC_ADDRESS_BY_NAME(module, name) GetProcAddress((module), (name))
#define GET_PROC_ADDRESS_BY_ORDINAL(module, name) GetProcAddress((module), (LPCSTR)(name))
#elif defined(__linux__)
	static const DWORD PageProtections[8] = {
		PROT_NONE,							// 0 ---
		PROT_EXEC,							// 1 --X
		PROT_WRITE,							// 2 -W-
		PROT_EXEC | PROT_WRITE,				// 3 -WX
		PROT_READ,							// 4 R--
		PROT_EXEC | PROT_READ,				// 5 R-X
		PROT_WRITE | PROT_READ,				// 6 RW-
		PROT_EXEC | PROT_WRITE | PROT_READ,	// 7 RWX
	};
// virtual memory functions
#define VIRTUAL_ALLOC(addr, size, protect) ({ addr = mmap(nullptr, (size), (protect), MAP_SHARED | MAP_ANONYMOUS, 0, 0); addr; })
#define VIRTUAL_PROTECT(addr, size, newProtect, oldProtect) (0 == mprotect((addr), (size), (newProtect)))

// module functions
#define MODULE_PTR void *
#define GET_MODULE_HANDLE(name) dlopen((name), RTLD_LAZY)
#define GET_PROC_ADDRESS_BY_NAME(module, name) dlsym((module), (name))
// function ordinals not present in linux
#define GET_PROC_ADDRESS_BY_ORDINAL(module, name) 0xFFFFFFFF
#endif

	void *InprocMapper::CreateSection(void *lpAddress, size_t dwSize, DWORD flProtect) {
		return VIRTUAL_ALLOC(lpAddress, dwSize, PageProtections[flProtect]);
	}

	bool InprocMapper::ChangeProtect(void *lpAddress, size_t dwSize, DWORD flProtect) {
		DWORD oldProtect;
		return VIRTUAL_PROTECT(lpAddress, dwSize, PageProtections[flProtect], oldProtect);
	}

	bool InprocMapper::WriteBytes(void *lpAddress, void *lpBuffer, size_t nSize) {
		memcpy(lpAddress, lpBuffer, nSize);
		return true;
	}

	DWORD InprocMapper::FindImport(const char *moduleName, const char *funcName) {
		MODULE_PTR hModule = GET_MODULE_HANDLE(moduleName);

		if (NULL == hModule) {
			return 0xFFFFFFFF;
		}

		return (DWORD)GET_PROC_ADDRESS_BY_NAME(hModule, funcName);
	}

	DWORD InprocMapper::FindImport(const char *moduleName, const unsigned int funcOrdinal) {
		MODULE_PTR hModule = GET_MODULE_HANDLE(moduleName);

		if (NULL == hModule) {
			return 0xFFFFFFFF;
		}
		return (DWORD)GET_PROC_ADDRESS_BY_ORDINAL(hModule, funcOrdinal);
	}

};