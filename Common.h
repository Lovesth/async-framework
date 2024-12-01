#pragma once

#include <stdexcept>
#include "CommonMacros.h"

namespace async_framework
{
    inline void logicAssert(bool x, const char *errorMsg)
    {
        if (x)
            AS_LIKELY { return; }
        throw std::logic_error(errorMsg);
    }
}