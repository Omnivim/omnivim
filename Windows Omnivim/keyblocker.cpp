#include <windows.h>
#include <iostream>
#include <set>
#include <string>
#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>

HHOOK keyboardHook;
std::set<DWORD> pressedKeys;

// Thread-safe queue for keys to inject
std::queue<WORD> injectionQueue;
std::mutex queueMutex;
std::condition_variable queueCV;
bool running = true;

std::string GetKeyName(DWORD vkCode) {
    UINT scanCode = MapVirtualKey(vkCode, MAPVK_VK_TO_VSC);
    if (vkCode == VK_SHIFT) scanCode = MapVirtualKey(VK_LSHIFT, MAPVK_VK_TO_VSC);
    if (vkCode == VK_RETURN || vkCode == VK_LEFT || vkCode == VK_RIGHT ||
        vkCode == VK_UP || vkCode == VK_DOWN)
        scanCode |= 0x01000000;

    char keyName[128];
    if (GetKeyNameTextA(scanCode << 16, keyName, sizeof(keyName))) {
        return std::string(keyName);
    } else {
        return "Unknown";
    }
}

void PrintPressedKeys() {
    std::cout << "\rCurrently Pressed Keys: ";
    for (DWORD vk : pressedKeys) {
        std::cout << GetKeyName(vk) << " ";
    }
    std::cout << "      " << std::flush;  // Clear leftover text with spaces
}

void SendArrowKey(WORD vkArrow) {
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = vkArrow;
    inputs[0].ki.dwFlags = 0;

    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = vkArrow;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;

    SendInput(2, inputs, sizeof(INPUT));
}

void InjectionThread() {
    while (running) {
        std::unique_lock<std::mutex> lock(queueMutex);
        queueCV.wait(lock, [] { return !injectionQueue.empty() || !running; });

        while (!injectionQueue.empty()) {
            WORD vk = injectionQueue.front();
            injectionQueue.pop();
            lock.unlock();

            SendArrowKey(vk);

            lock.lock();
        }
    }
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* pKeyboard = (KBDLLHOOKSTRUCT*)lParam;

        // Check if event was injected — if so, let it pass without suppressing
        if (pKeyboard->flags & LLKHF_INJECTED) {
            return CallNextHookEx(NULL, nCode, wParam, lParam);
        }

        DWORD vkCode = pKeyboard->vkCode;

        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            if (pressedKeys.find(vkCode) == pressedKeys.end()) {
                pressedKeys.insert(vkCode);
                PrintPressedKeys();
            }

            if (vkCode == VK_ESCAPE) {
                std::cout << "\nESCAPE detected. Exiting...\n";
                PostQuitMessage(0);
            }

            if (vkCode == 'H' || vkCode == 'J' || vkCode == 'K' || vkCode == 'L') {
                WORD arrowKey = 0;
                switch (vkCode) {
                    case 'H': arrowKey = VK_LEFT; break;
                    case 'J': arrowKey = VK_DOWN; break;
                    case 'K': arrowKey = VK_UP; break;
                    case 'L': arrowKey = VK_RIGHT; break;
                }
                {
                    std::lock_guard<std::mutex> lock(queueMutex);
                    injectionQueue.push(arrowKey);
                }
                queueCV.notify_one();

                return 1; // suppress original key
            }

            return 1; // optionally suppress other keys
        }

        if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
            if (pressedKeys.find(vkCode) != pressedKeys.end()) {
                pressedKeys.erase(vkCode);
                PrintPressedKeys();
            }
            return 1; // suppress keyup
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

int main() {
    std::cout << "Omnivim Starting... Press ESC to exit.\n";

    std::thread injector(InjectionThread);

    HINSTANCE hInstance = GetModuleHandle(NULL);
    keyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, hInstance, 0);

    if (!keyboardHook) {
        std::cerr << "Failed to install hook!" << std::endl;
        running = false;
        queueCV.notify_one();
        injector.join();
        return 1;
    }

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    running = false;
    queueCV.notify_one();
    injector.join();

    UnhookWindowsHookEx(keyboardHook);
    return 0;
}
