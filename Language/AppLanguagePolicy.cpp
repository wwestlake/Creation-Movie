#include "AppLanguagePolicy.h"

#if CM_ENABLE_SCRIPTING
 #include <llvm/Config/llvm-config.h>
#endif

namespace creation_movie::language
{
std::string getLanguageRuntimeSummary()
{
#if CM_ENABLE_SCRIPTING
    return "LLVM-enabled host layer ready for video-domain scripting (" LLVM_VERSION_STRING ")";
#else
    return "LLVM scripting disabled for this build.";
#endif
}

std::string getAppDomainName()
{
    return "movie";
}

bool canRunNodeDomain(const std::string& domainName)
{
    return domainName == "shared" || domainName == "movie" || domainName == "timeline" || domainName == "render";
}
}

