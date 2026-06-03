#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <reshade.hpp>

// هدر رسمی ری‌شید برای ارتباط با موتور رندر
using namespace reshade::api;

// آفست‌های استخراج شده از کد سایدر شما
const uintptr_t BASE_OFFSET = 0x037F89E0;
const uintptr_t NEW_BASE_OFFSET = 0x036F0260;

// تابع کمکی برای خواندن امن حافظه جهت جلوگیری از کرش بازی (Crash to Desktop)
template <typename T>
T SafeRead(uintptr_t address, T defaultValue = T()) {
    __try {
        if (address < 0x10000 || address > 0x7FFFFFFFEFFF) return defaultValue; // محدوده امن مموری ۶۴ بیتی
        return *reinterpret_cast<T*>(address);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return defaultValue;
    }
}

// این تابع در هر فریم (موقع رندر Present) اجرا می‌شود
static void on_present(command_queue *queue, effect_runtime *runtime)
{
    // دریافت آدرس پایه بازی در حال اجرا (مثل pes2021.exe) که جایگزین 0x140000000 ثابت می‌شود
    uintptr_t baseAddress = reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr));

    // ---- پوینتر اول: مقدار چرخش دوربین ----
    uintptr_t ptr1 = SafeRead<uintptr_t>(baseAddress + BASE_OFFSET);
    if (ptr1) ptr1 = SafeRead<uintptr_t>(ptr1 + 0x138);
    if (ptr1) ptr1 = SafeRead<uintptr_t>(ptr1 + 0x20);
    if (ptr1) ptr1 = SafeRead<uintptr_t>(ptr1 + 0x8);
    float liveRotation = ptr1 ? SafeRead<float>(ptr1 + 0xC) : -999.0f; // اگر لود نشده باشد -999 می‌دهد

    // ---- پوینتر دوم: مقدار پوینتر جدید ----
    uintptr_t ptr2 = SafeRead<uintptr_t>(baseAddress + NEW_BASE_OFFSET);
    if (ptr2) ptr2 = SafeRead<uintptr_t>(ptr2 + 0x138);
    if (ptr2) ptr2 = SafeRead<uintptr_t>(ptr2 + 0x20);
    if (ptr2) ptr2 = SafeRead<uintptr_t>(ptr2 + 0x8);
    float liveNewValue = ptr2 ? SafeRead<float>(ptr2 + 0x10) : -999.0f;

    // ---- تزریق مستقیم به متغیرهای شیدر ری‌شید ----
    // افزونه متغیرهایی با نام های uLiveRotation و uLiveNewValue را در شیدرها پیدا کرده و اوررایت میکند
    runtime->set_uniform_value_float(runtime->find_uniform_variable(nullptr, "uLiveRotation"), liveRotation);
    runtime->set_uniform_value_float(runtime->find_uniform_variable(nullptr, "uLiveNewValue"), liveNewValue);
}

// معرفی مشخصات افزونه به ری‌شید برای نمایش در منوی بازی
extern "C" __declspec(dllexport) const char *NAME = "Fox Memory Bridge";
extern "C" __declspec(dllexport) const char *DESCRIPTION = "Bridges Fox Engine memory values to ReShade uniforms.";

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    switch (fdwReason)
    {
    case DLL_PROCESS_ATTACH:
        if (!reshade::register_addon(hinstDLL))
            return FALSE;
        // ثبت رویداد برای اجرا در هر فریم بر اساس داکیومنت جدید ری‌شید
        reshade::register_event<reshade::addon_event::present>(on_present);
        break;
    case DLL_PROCESS_DETACH:
        reshade::unregister_addon(hinstDLL);
        break;
    }
    return TRUE;
}
