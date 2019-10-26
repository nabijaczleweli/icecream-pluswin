/* -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 99; -*- */
/* vim: set ts=4 sw=4 et tw=99:  */
/*
    This file is part of Icecream.

    Copyright (c) 2006 Mirko Boehm <mirko@kde.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "logging.h"
#include "platform.h"

#ifdef _WIN32
/* https://github.com/ThePhD/infoware/blob/8dd5c4e2c126641f5c6a26f8a6e82decd31b494d/src/system/OS_info/os_info_windows.cpp
   https://github.com/ThePhD/infoware/blob/8dd5c4e2c126641f5c6a26f8a6e82decd31b494d/src/detail/winstring_windows.cpp */

#define WIN32_LEAN_AND_MEAN
#include <wbemidl.h>
#include <windows.h>
#include <memory>
#include <algorithm>

namespace {
    struct release_deleter {
        template <typename T>
        void operator()(T* p) const {
            p->Release();
        }
    };


    template<class F>
    struct scope_end {
        F func;

        ~scope_end() {
            func();
        }
    };

    template<class F>
    static scope_end<F> on_scope_end(F && func) {
        return {func};
    }

    static std::string narrowen_bstring(const wchar_t * bstr) {
        if(!bstr)
            return {};

        const auto bstr_size = SysStringLen(const_cast<BSTR>(bstr));

        std::string ret;
        // convert even embedded NUL
        if(const auto len = WideCharToMultiByte(CP_UTF8, 0, bstr, static_cast<int>(bstr_size), NULL, 0, 0, 0)) {
            ret.resize(len, '\0');
            if(!WideCharToMultiByte(CP_UTF8, 0, bstr, static_cast<int>(bstr_size), &ret[0], len, 0, 0))
                ret.clear();
        }
        return ret;
    }
}

std::string determine_platform_once() {
    if(FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
        log_perror("CoInitializeEx call failed");
        throw("determine_platform: cannot determine OS version and machine architecture");
    }
    auto com_uninitialiser = on_scope_end(CoUninitialize);

    const auto init_result =
        CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
    if(FAILED(init_result) && init_result != RPC_E_TOO_LATE) {
        log_perror("CoInitializeSecurity call failed");
        throw("determine_platform: cannot determine OS version and machine architecture");
    }

    IWbemLocator* wbem_loc_raw;
    if(FAILED(CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, reinterpret_cast<void **>(&wbem_loc_raw)))) {
        log_perror("CoCreateInstance call failed");
        throw("determine_platform: cannot determine OS version and machine architecture");
    }
    std::unique_ptr<IWbemLocator, release_deleter> wbem_loc(wbem_loc_raw);

    IWbemServices* wbem_services_raw;
    wchar_t network_resource[] = LR"(ROOT\CIMV2)";
    if(FAILED(wbem_loc->ConnectServer(network_resource, nullptr, nullptr, 0, 0, 0, 0, &wbem_services_raw))) {
        log_perror("ConnectServer call failed");
        throw("determine_platform: cannot determine OS version and machine architecture");
    }
    std::unique_ptr<IWbemServices, release_deleter> wbem_services(wbem_services_raw);

    if(FAILED(CoSetProxyBlanket(wbem_services.get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr,
                                EOAC_NONE))) {
        log_perror("CoSetProxyBlanket call failed");
        throw("determine_platform: cannot determine OS version and machine architecture");
    }

    IEnumWbemClassObject* query_iterator_raw;
    wchar_t query_lang[] = L"WQL";
    wchar_t query[]      = L"SELECT Name FROM Win32_OperatingSystem";
    if(FAILED(wbem_services->ExecQuery(query_lang, query, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &query_iterator_raw)))
        return {};
    std::unique_ptr<IEnumWbemClassObject, release_deleter> query_iterator(query_iterator_raw);

    std::string platform;
    while(query_iterator) {
        IWbemClassObject* value_raw;
        unsigned long iter_result;

        query_iterator->Next(static_cast<LONG>(WBEM_INFINITE), 1, &value_raw, &iter_result);
        if(!iter_result)
            break;
        std::unique_ptr<IWbemClassObject, release_deleter> value(value_raw);

        VARIANT val;
        value->Get(L"Name", 0, &val, 0, 0);
        auto val_destructor = on_scope_end([&] { VariantClear(&val); });

        platform = narrowen_bstring(val.bstrVal);
    }
    platform.erase(platform.find('|'));

    platform += '_';

    SYSTEM_INFO platform_info;
    GetNativeSystemInfo(&platform_info);
    switch(platform_info.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64:
            platform += "x86_64";
            break;

        case PROCESSOR_ARCHITECTURE_ARM:
            platform += "ARM";
            break;

        case PROCESSOR_ARCHITECTURE_ARM64:
            platform += "ARM64";
            break;

        case PROCESSOR_ARCHITECTURE_IA64:
            platform += "IA64";
            break;

        case PROCESSOR_ARCHITECTURE_INTEL:
            platform += "x86";
            break;

        case PROCESSOR_ARCHITECTURE_UNKNOWN:
            log_perror("PROCESSOR_ARCHITECTURE_UNKNOWN");
            throw("determine_platform: cannot determine OS version and machine architecture");
    }

    platform.erase(std::remove(platform.begin(), platform.end(), ' '), platform.end());
    return platform;
}
#else
extern "C" {
#include <sys/utsname.h>
}

std::string determine_platform_once()
{
    using namespace std;
    string platform;

    struct utsname uname_buf;

    if (uname(&uname_buf)) {
        log_perror("uname call failed");
        throw("determine_platform: cannot determine OS version and machine architecture");
        // return platform;
    }

    string os = uname_buf.sysname;

    if (os == "Darwin") {
        const std::string release = uname_buf.release;
        const string::size_type pos = release.find('.');

        if (pos == string::npos) {
            throw(std::string("determine_platform: Cannot determine Darwin release from release string \"") + release + "\"");
        }

        os += release.substr(0, pos);
    }

    if (os != "Linux") {
        platform = os + '_' + uname_buf.machine;
    } else { // Linux
        platform = uname_buf.machine;
    }

    while (true) {
        string::size_type pos = platform.find(" ");

        if (pos == string::npos) {
            break;
        }

        platform.erase(pos, 1);
    }

    return platform;
}
#endif

const std::string &determine_platform()
{
    const static std::string platform(determine_platform_once());
    return platform;
}
