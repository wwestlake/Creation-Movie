#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>
#endif
#include "MovieWebCamNetwork.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <tuple>

namespace movie {
std::vector<std::string> selectWebCamAddresses(std::vector<WebCamInterface> interfaces)
{
    std::erase_if(interfaces, [](const auto& i) {
        if (!i.up || (!i.physical && !i.gateway && !i.tunnel)) return true;
        auto a = juce::StringArray::fromTokens(juce::String(i.address), ".", "");
        if (a.size() != 4) return true;
        for (const auto& x : a) if (x.isEmpty() || !x.containsOnly("0123456789") || x.getIntValue() > 255) return true;
        return !(a[0] == "10" || (a[0] == "192" && a[1] == "168") ||
                 (a[0] == "172" && a[1].getIntValue() >= 16 && a[1].getIntValue() <= 31));
    });
    auto rank = [](const auto& i) { return std::tuple(i.physical ? 0 : i.tunnel ? 2 : 1, i.metric, i.address); };
    std::stable_sort(interfaces.begin(), interfaces.end(), [&](const auto& a, const auto& b) { return rank(a) < rank(b); });
    std::vector<std::string> result;
    for (const auto& i : interfaces)
        if (std::find(result.begin(), result.end(), i.address) == result.end() && result.size() < 16) result.push_back(i.address);
    return result;
}
std::vector<std::string> discoverWebCamAddresses()
{
    std::vector<WebCamInterface> found;
#ifdef _WIN32
    ULONG size = 16384;
    std::vector<unsigned char> buffer(size);
    ULONG result = ERROR_BUFFER_OVERFLOW;
    for (int attempt = 0; attempt < 3 && result == ERROR_BUFFER_OVERFLOW; ++attempt) {
        buffer.resize(size);
        result = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_GATEWAYS, nullptr,
            reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
    }
    if (result != NO_ERROR) throw std::runtime_error("Cannot discover local networks. Check your PC network connection and try again.");
    for (auto* adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()); adapter; adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp || adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        MIB_IF_ROW2 row{}; row.InterfaceLuid = adapter->Luid;
        const bool physical = GetIfEntry2(&row) == NO_ERROR && row.InterfaceAndOperStatusFlags.HardwareInterface;
        const bool tunnel = adapter->IfType == IF_TYPE_TUNNEL || adapter->IfType == IF_TYPE_PPP;
        for (auto* a = adapter->FirstUnicastAddress; a; a = a->Next) {
            if (a->Address.lpSockaddr->sa_family != AF_INET || a->DadState != IpDadStatePreferred) continue;
            char address[INET_ADDRSTRLEN]{};
            const auto* ipv4 = reinterpret_cast<sockaddr_in*>(a->Address.lpSockaddr);
            if (InetNtopA(AF_INET, const_cast<IN_ADDR*>(&ipv4->sin_addr), address, sizeof(address)))
                found.push_back({address, true, physical, adapter->FirstGatewayAddress != nullptr, tunnel, adapter->Ipv4Metric});
        }
    }
#else
    throw std::runtime_error("Automatic WebCam discovery currently requires Windows.");
#endif
    return selectWebCamAddresses(std::move(found));
}
}
