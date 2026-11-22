#include <iostream>
#include <string_view>

int main()
{
    constexpr std::string_view projectName = "IPTV M10";
    constexpr std::string_view version = "0.1.0";

    std::cout << "=========================\n";
    std::cout << projectName << '\n';
    std::cout << "Version: " << version << '\n';
    std::cout << "C++20 backend initialized\n";
    std::cout << "=========================\n";

    return 0;
}
