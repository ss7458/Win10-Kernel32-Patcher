
#include <windows.h>
#include <stdio.h>

// Static import declaration - linker resolves from kernel32
EXTERN_C HRESULT WINAPI GetThreadDescription(HANDLE hThread, PWSTR* ppszThreadDescription);

int main() {
    PWSTR desc = NULL;
    HRESULT hr = GetThreadDescription(GetCurrentThread(), &desc);
    printf("GetThreadDescription returned 0x%08X, desc=%p\n", hr, (void*)desc);
    if (desc) LocalFree(desc);
    printf("Static-import test program loaded OK\n");
    return 0;
}
