#pragma once
#include "OpenKNX/defines.h"
#include <Arduino.h>
#include <string>

#define OPENKNX_BUTTON_DEBOUNCE 50
#define OPENKNX_BUTTON_DOUBLE_CLICK 500
#define OPENKNX_BUTTON_LONG_CLICK 1000

typedef void (*ShortClickCallbackFunction)();
typedef void (*LongClickCallbackFunction)();
typedef void (*DoubleClickCallbackFunction)();

namespace OpenKNX
{
    class Button
    {
      private:
        const char *_id;
        volatile uint32_t _holdTimer = 0;
        volatile uint32_t _dblClickTimer = 0;

        ShortClickCallbackFunction _shortClickCallback = nullptr;
        LongClickCallbackFunction _longClickCallback = nullptr;
        DoubleClickCallbackFunction _doubleClickCallback = nullptr;

      public:
        Button(const char *id) : _id(id){};
        void change(bool pressed);
        void loop();

        void onShortClick(ShortClickCallbackFunction shortClickCallback) { _shortClickCallback = shortClickCallback; }
        void onLongClick(LongClickCallbackFunction longClickCallback) { _longClickCallback = longClickCallback; }
        void onDoubleClick(DoubleClickCallbackFunction doubleCallback) { _doubleClickCallback = doubleCallback; }

        inline void callShortClickCallback();
        inline void callLongClickCallback();
        inline void callDoubleClickCallback();

        std::string logPrefix();
    };
} // namespace OpenKNX