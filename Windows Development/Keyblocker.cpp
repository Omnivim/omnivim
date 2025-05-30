#include <windows.h>
#include <iostream>
#include <set>
#include <string>
#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <vector>
#include <map>
HHOOK keyboardHook;
std::set<DWORD> pressedKeys;
bool gPressed = false;
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

void InjectionThread() {
    while (running) {
        std::unique_lock<std::mutex> lock(queueMutex);
        queueCV.wait(lock, [] { return !injectionQueue.empty() || !running; });

        // Collect all keys currently in the queue
        std::vector<WORD> keysToInject;
        while (!injectionQueue.empty()) {
            keysToInject.push_back(injectionQueue.front());
            injectionQueue.pop();
        }
        lock.unlock();

        if (!keysToInject.empty()) {
            std::vector<INPUT> inputs;
            inputs.reserve(keysToInject.size() * 2);

            // Add keydown events
            for (WORD vk : keysToInject) {
                INPUT down = {};
                down.type = INPUT_KEYBOARD;
                down.ki.wVk = vk;
                down.ki.dwFlags = 0;
                inputs.push_back(down);
            }
            // Add keyup events
            for (WORD vk : keysToInject) {
                INPUT up = {};
                up.type = INPUT_KEYBOARD;
                up.ki.wVk = vk;
                up.ki.dwFlags = KEYEVENTF_KEYUP;
                inputs.push_back(up);
            }

            SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
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
            if (vkCode == 'G') {
                gPressed = true;
            }
            if (vkCode == VK_ESCAPE) {
                std::cout << "\nESCAPE detected. Exiting...\n";
                PostQuitMessage(0);
            }

            // Define a mapping from input keys to virtual key codes
            std::map<WORD, std::vector<WORD>> keyMappings = {
                {'H', {VK_LEFT}},
                {'J', {VK_DOWN}},
                {'K', {VK_UP}},
                {'L', {VK_RIGHT}},
                {'W', {VK_CONTROL, VK_RIGHT}},
                {'B', {VK_CONTROL, VK_LEFT}},
                {'E', {VK_CONTROL, VK_RIGHT}}, // Simplification
                {'0', {VK_HOME}},
                {'M', {VK_HOME}}, // Simplification
                {'L', {VK_END}},
            };

            auto it = keyMappings.find(vkCode);
            if (it != keyMappings.end()) {
                {
                    std::lock_guard<std::mutex> lock(queueMutex);
                    for (WORD key : it->second) {
                        injectionQueue.push(key);
                    }
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
            if (vkCode == 'G') {
                gPressed = false;
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
