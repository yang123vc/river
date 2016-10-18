#ifndef _ABSTRACT_LDR_H_
#define _ABSTRACT_LDR_H_

#include "Abstract.Mapper.h"

namespace ldr {
	class AbstractBinary {
	public:
		virtual bool IsValid() const = 0;
		virtual bool Map(AbstractPEMapper &mapr, DWORD &baseAddr) = 0;
	};
}; //namespace ldr

#endif
