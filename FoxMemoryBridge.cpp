#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <reshade.hpp>

using namespace reshade::api;

const uintptr_t BASE_OFFSET = 0x037F89E0;
const uintptr_t NEW_BASE_OFFSET = 0x036F0260;

template <typename T>
T SafeRead(uintptr_t address, T defaultValue = T()) {
    __try {
        if (address < 0x10000 || address > 0x7FFFFFFFEFFF) return defaultValue;
        return *reinterpret_cast<T*>(address);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return defaultValue;
    }
}

// سینتکس آپدیت شده مخصوص ReShade جدید
static void on_present(command_queue *queue, swapchain *swapchain, const rect *source_rect, const rect *dest_rect, uint32_t dirty_rect_count, const rect *dirty_rects)
{
    // دریافت effect_runtime از طریق swapchain
    effect_runtime *runtime = static_cast<effect_runtime*>(swapchain);
    uintptr_t baseAddress = reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));

    // پوینتر اول
    uintptr_t ptr1 = SafeRead<uintptr_t>(baseAddress + BASE_OFFSET);
    if (ptr1) ptr1 = SafeRead<uintptr_t>(ptr1 + 0x138);
    if (ptr1) ptr1 = SafeRead<uintptr_t>(ptr1 + 0x20);
    if (ptr1) ptr1 = SafeRead<uintptr_t>(ptr1 + 0x8);
    float liveRotation = ptr1 ? SafeRead<float>(ptr1 + 0xC) : -999.0f;

    // پوینتر دوم
    uintptr_t ptr2 = SafeRead<uintptr_t>(baseAddress + NEW_BASE_OFFSET);
    if (ptr2) ptr2 = SafeRead<uintptr_t>(ptr2 + 0x138);
    if (ptr2) ptr2 = SafeRead<uintptr_t>(ptr2 + 0x20);
    if (ptr2) ptr2 = SafeRead<uintptr_t>(ptr2 + 0x8);
    float liveNewValue = ptr2 ? SafeRead<float>(ptr2 + 0x10) : -999.0f;

    // تزریق مقادیر به شیدر
    runtime->set_uniform_value_float(runtime->find_uniform_variable(nullptr, "uLiveRotation"), liveRotation);
    runtime->set_uniform_value_float(runtime->find_uniform_variable(nullptr, "uLiveNewValue"), liveNewValue);
}

extern "C" __declspec(dllexport) const char *GetReShadeVersion() { return RESHADE_API_VERSION_STRING; }

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
        if (!reshade::register_addon(hinstDLL))
            return FALSE;
        reshade::register_event<reshade::api::event_id::present>(on_present);
        break;
    case DLL_PROCESS_DETACH:
        reshade::unregister_addon(hinstDLL);
        break;
    }
    return TRUE;
}
