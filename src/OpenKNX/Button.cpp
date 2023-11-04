#include "OpenKNX/Button.h"
#include "OpenKNX/Facade.h"

namespace OpenKNX
{
    void __time_critical_func(Button::loop)()
    {
        if (_dblClickTimer && delayCheck(_dblClickTimer, OPENKNX_BUTTON_DOUBLE_CLICK))
        {
            // Short
            callShortClickCallback();
            _dblClickTimer = 0;
        }

        if (_holdTimer && delayCheck(_holdTimer, OPENKNX_BUTTON_LONG_CLICK))
        {
            // Long
            callLongClickCallback();
            _holdTimer = 0;
        }
    }

    void __time_critical_func(Button::change)(bool pressed)
    {
#ifdef OPENKNX_RTT
        // logTraceP("Event %i", pressed);
#endif
        if (pressed) // Press
        {
            // first press?
            if (!_holdTimer) _holdTimer = millis();
        }
        else // Release
        {
            // Debounce
            if (_holdTimer && delayCheck(_holdTimer, OPENKNX_BUTTON_DEBOUNCE))
            {
                if (_doubleClickCallback == nullptr)
                {
                    callShortClickCallback();
                }
                else
                {
                    if (_dblClickTimer)
                    {
                        _dblClickTimer = 0;
                        callDoubleClickCallback();
                    }
                    else
                    {
                        _dblClickTimer = millis();
                    }
                }
            }

            _holdTimer = 0;
        }
    }

    void Button::callShortClickCallback()
    {
#ifdef OPENKNX_RTT
        logTraceP("ShortClick");
#endif
        if (_shortClickCallback != nullptr) _shortClickCallback();
    }

    void Button::callLongClickCallback()
    {
#ifdef OPENKNX_RTT
        logTraceP("LongClick");
#endif
        if (_longClickCallback != nullptr) _longClickCallback();
    }

    void Button::callDoubleClickCallback()
    {
#ifdef OPENKNX_RTT
        logTraceP("DoubleClick");
#endif
        if (_doubleClickCallback != nullptr) _doubleClickCallback();
    }

    std::string Button::logPrefix()
    {
        return openknx.logger.buildPrefix("Button", _id);
    }

} // namespace OpenKNX