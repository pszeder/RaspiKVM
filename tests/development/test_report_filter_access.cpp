#include "../../raspikey/ReportFilterAccess.h"
#include <atomic>
#include <cassert>
#include <iostream>
#include <thread>
struct Filter : IReportFilter {
    size_t ProcessInputReport(uint8_t*,size_t n) override {return n;}
    size_t ProcessOutputReport(uint8_t*,size_t n) override {return n;}
    std::string GetSettings() override {return "{}";}
    void SetSettings(std::string value) override {assert(value=="{}");}
};
int main() {
    std::mutex mutex;
    IReportFilter* filter=nullptr;
    bool readRejected=false,writeRejected=false;
    try{ReadFilterSettings(mutex,filter);}catch(const std::runtime_error&){readRejected=true;}
    try{WriteFilterSettings(mutex,filter,"{}");}catch(const std::runtime_error&){writeRejected=true;}
    assert(readRejected&&writeRejected);
    filter=new Filter;
    assert(ReadFilterSettings(mutex,filter)=="{}");
    WriteFilterSettings(mutex,filter,"{}");
    std::atomic<unsigned> iterations{0};
    std::thread reader([&]{
        for(unsigned i=0;i!=10000;++i){
            try{assert(ReadFilterSettings(mutex,filter)=="{}");}catch(const std::runtime_error&){}
            try{WriteFilterSettings(mutex,filter,"{}");}catch(const std::runtime_error&){}
            ++iterations;
        }
    });
    for(unsigned i=0;i!=1000;++i){
        {std::lock_guard<std::mutex> guard(mutex);delete filter;filter=nullptr;}
        {std::lock_guard<std::mutex> guard(mutex);filter=new Filter;}
    }
    reader.join();
    delete filter;
    assert(iterations==10000);
    std::cout<<"Disconnected settings are rejected; reconnect/read/write lifetime checks passed\n";
}
