#include "../../raspikey/A1314.h"
#include "../../raspikey/A1644.h"
#include "../../raspikey/KeyboardReportSafety.h"
#include <array>
#include <cassert>
#include <iostream>

using Report = std::array<uint8_t,10>;
int main()
{
    for(bool swap : {false,true}) {
        A1314 keyboard;
        keyboard.SetSettings(swap ? "{\"swapFnCtrl\":true,\"swapAltCmd\":false}"
                                  : "{\"swapFnCtrl\":false,\"swapAltCmd\":false}");
        Report press{{1,0,0,4}};
        assert(keyboard.ProcessInputReport(press.data(),9)==10 && press[3]==4);
        for(uint8_t error : {1,2,3}) {
            Report ambiguous{{1,0,0,error,error,error,error,error,error}};
            assert(keyboard.ProcessInputReport(ambiguous.data(),9)==0);
        }
        // Separate special report uses the last valid physical key, not errors.
        Report special{{0x11,0}};
        assert(keyboard.ProcessInputReport(special.data(),2)==10);
        assert(special[3]==4 && special[1]==0);
        Report release{{1}};
        assert(keyboard.ProcessInputReport(release.data(),9)==10);
        assert(release[1]==0 && release[3]==0);
        Report unknown{{1,0,0,0x17}};
        assert(keyboard.ProcessInputReport(unknown.data(),4)==0);

        // A real Ctrl+Shift+T chord is still passed when unambiguously reported.
        if(swap) {
            Report fn{{0x11,0x10}};
            assert(keyboard.ProcessInputReport(fn.data(),2)==10 && fn[1]==1);
        }
        Report chord{{1,uint8_t(swap ? 2 : 3),0,0x17}};
        assert(keyboard.ProcessInputReport(chord.data(),9)==10);
        assert(chord[1]==3 && chord[3]==0x17);
        Report none{{1}};
        assert(keyboard.ProcessInputReport(none.data(),9)==10);
        if(swap) {
            Report fnRelease{{0x11,0}};
            assert(keyboard.ProcessInputReport(fnRelease.data(),2)==10);
            assert(fnRelease[1]==0 && fnRelease[3]==0);
        }
    }
    A1644 keyboard;
    for(uint8_t error : {1,2,3}) {
        Report ambiguous{{1,0,0,error,error,error,error,error,error}};
        assert(keyboard.ProcessInputReport(ambiguous.data(),10)==0);
    }
    Report chord{{1,2,0,0x17,0,0,0,0,0,2}};
    assert(keyboard.ProcessInputReport(chord.data(),10)==10);
    assert(chord[1]==3 && chord[3]==0x17);
    Report release{{1}};
    assert(keyboard.ProcessInputReport(release.data(),10)==10 && release[1]==0);
    assert(KeyboardErrorReports().load()==9);
    std::cout << "Actual A1314/A1644 filters: error suppression, valid key cache, releases, Fn/Ctrl and real shortcuts passed\n";
}
