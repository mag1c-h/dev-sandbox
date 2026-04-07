/**
 * MIT License
 *
 * Copyright (c) 2026 Mag1c.H
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * */
#include <memory>
#include <unordered_map>
#include "copy_case.h"

struct CaseArgs {
    std::string name;
    size_t ioSize{512 * 1024 * 1024};
    size_t ioNumber{8};
    size_t iterNumber{128};
    size_t deviceNumber{8};

    static void Help(const char* proc)
    {
        fmt::println("Usage: {} [options]", proc);
        fmt::println("Options:");
        fmt::println("  -t <name>        Case name");
        fmt::println("  -s <size>        Data size in KB/MB (e.g., 4K, 16K, 1M, default: 512MB)");
        fmt::println("  -n <count>       Data number (default: 8)");
        fmt::println("  -i <count>       Iteration count (default: 128)");
        fmt::println("  -d <count>       Number of devices (default: 8)");
    }
    CaseArgs(int argc, char const* argv[])
    {
        for (auto i = 1; i < argc; i++) {
            std::string arg{argv[i]};
            if (arg == "-t" && i + 1 < argc) {
                name = argv[++i];
            } else if (arg == "-s" && i + 1 < argc) {
                std::string sizeStr{argv[++i]};
                char unit = sizeStr.back();
                size_t multiplier = 1;
                if (unit == 'K' || unit == 'k') {
                    multiplier = 1024;
                    sizeStr.pop_back();
                } else if (unit == 'M' || unit == 'm') {
                    multiplier = 1024 * 1024;
                    sizeStr.pop_back();
                } else {
                    fmt::println("Invalid size unit. Use K for KB or M for MB.");
                    exit(EXIT_FAILURE);
                }
                ioSize = std::stoull(sizeStr) * multiplier;
            } else if (arg == "-n" && i + 1 < argc) {
                ioNumber = std::stoull(argv[++i]);
            } else if (arg == "-i" && i + 1 < argc) {
                iterNumber = std::stoull(argv[++i]);
            } else if (arg == "-d" && i + 1 < argc) {
                deviceNumber = std::stoull(argv[++i]);
            } else {
                Help(argv[0]);
                exit(EXIT_SUCCESS);
            }
        }
    }
};

std::unordered_map<std::string, std::shared_ptr<CopyCase>> MakeAllCases()
{
    std::vector<std::shared_ptr<CopyCase>> array = {
        std::make_shared<Host2DeviceCECase>(),         std::make_shared<Host2DeviceSMCase>(),
        std::make_shared<OneHost2AllDeviceCECase>(),   std::make_shared<OneHost2AllDeviceSMCase>(),
        std::make_shared<AllHost2AllDeviceCECase>(),   std::make_shared<Device2DeviceCECase>(),
        std::make_shared<OneDevice2AllDeviceCECase>(),
    };
    std::unordered_map<std::string, std::shared_ptr<CopyCase>> cases;
    for (auto c : array) { cases[c->Key()] = c; }
    return cases;
}

int main(int argc, char const* argv[])
{
    CaseArgs args{argc, argv};
    if (args.name.empty()) {
        CaseArgs::Help(*argv);
        return -1;
    }
    auto cases = MakeAllCases();
    auto iter = cases.find(args.name);
    if (iter == cases.end()) {
        for (auto e : cases) { fmt::println("{:<32}: {}", e.first, e.second->Brief()); }
        return -1;
    }
    iter->second->Run(args.ioSize, args.ioNumber, args.iterNumber, args.deviceNumber);
    return 0;
}
