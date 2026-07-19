
#include <windows.h>
#include <stdio.h>

// Declare the API we want to test (normally from kernel32 on Win10 1607+)
typedef HRESULT (WINAPI *GetThreadDescription_t)(HANDLE, PWSTR*);

int main() {
    // Try to load the API from kernel32 (will fail on older systems)
    HMODULE h = GetModuleHandleA("kernel32.dll");
    FARPROC p = h ? GetProcAddress(h, "GetThreadDescription") : NULL;
    if (p) {
        printf("GetThreadDescription found in kernel32 at %p\\n", p);
    } else {
        printf("GetThreadDescription NOT in kernel32 (expected on old systems)\\n");
    }
    printf("Test program loaded OK\\n");
    return 0;
}
