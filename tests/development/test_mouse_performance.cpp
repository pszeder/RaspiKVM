#include "../../raspikey/MouseTiming.h"
#include "../../raspikey/MouseLinkProtocol.h"
#include <cassert>
#include <iostream>

int main() {
    using namespace MouseLinkProtocol;
    std::array<uint8_t,6> address{};
    assert(Address("d1:65:57:8c:db:c1",address));
    assert((address==std::array<uint8_t,6>{{0xc1,0xdb,0x8c,0x57,0x65,0xd1}}));
    for(auto invalid:{"D1:65:57:8C:DB","D1:65:57:8C:DB:C1:00","D1:65:57:8C:DB:G1","D1-65:57:8C:DB:C1"})assert(!Address(invalid,address));
    auto request=Request(0x0102,address,2);
    assert((request==std::array<uint8_t,23>{{0x35,0,2,1,17,0,1,0,0xc1,0xdb,0x8c,0x57,0x65,0xd1,2,6,0,6,0,0,0,216,0}}));
    uint8_t status=0;
    uint8_t complete[]={1,0,2,1,3,0,0x35,0,0};
    uint8_t failure[]={2,0,2,1,3,0,0x0c,0x35,0};
    assert(CommandResult(complete,sizeof(complete),0x0102,status)&&status==0);
    assert(CommandResult(failure,sizeof(failure),0x0102,status)&&status==0x0c);
    assert(!CommandResult(complete,sizeof(complete),0,status));
    for(size_t n=0;n<sizeof(complete);++n)assert(!CommandResult(complete,n,0x0102,status));
    uint8_t update[]={4,0x3e,10,3,0,64,0,6,0,0,0,216,0};
    Update result;
    assert(ConnectionUpdate(update,sizeof(update),64,result));
    assert(result.status==0&&result.interval==6&&result.latency==0&&result.timeout==216);
    assert(!ConnectionUpdate(update,sizeof(update),65,result));
    for(size_t n=0;n<sizeof(update);++n)assert(!ConnectionUpdate(update,n,64,result));
    update[3]=2;assert(!ConnectionUpdate(update,sizeof(update),64,result));
    MouseTiming timing;
    for(uint64_t i=0;i<100;++i){uint64_t stamp=1000000+i*11250;timing.Frame(stamp,stamp+200,stamp+250);}
    assert(timing.frames==100&&timing.gaps==99&&timing.gapSum/timing.gaps==11250);
    assert(timing.queueSum/timing.frames==250&&timing.readSum/timing.frames==200&&timing.writeSum/timing.frames==50);
    assert(MouseTiming::Percentile(timing.gapHistogram,timing.gaps,50,500)==11500);
    assert(MouseTiming::Percentile(timing.queueHistogram,timing.frames,95,100)==300);
    timing.Frame(3000000,3000200,3000250);assert(timing.gaps==99); // Pause excluded.
    timing.Frame(3007500,3007700,3007750);assert(timing.gaps==100);
    timing.Frame(4000000,3999999,4000001);assert(timing.frames==102); // Invalid clocks rejected.
    MouseTiming overflow;overflow.Frame(100,200,10100);
    assert(overflow.queueHistogram.back()==1&&MouseTiming::Percentile(overflow.queueHistogram,1,95,100)==0);
    std::cout<<"Mouse link protocol and aggregate timing tests passed\n";
}
