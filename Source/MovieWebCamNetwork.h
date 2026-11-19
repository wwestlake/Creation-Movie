#pragma once
#include <string>
#include <vector>

namespace movie {
struct WebCamInterface {
    std::string address;
    bool up = false, physical = false, gateway = false, tunnel = false;
    unsigned metric = 0;
};
// Pure selection policy is exercised with physical, virtual and VPN fixtures.
std::vector<std::string> selectWebCamAddresses(std::vector<WebCamInterface>);
std::vector<std::string> discoverWebCamAddresses();
}
