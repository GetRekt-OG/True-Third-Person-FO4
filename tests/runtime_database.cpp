#include "REL/RuntimeDatabaseKnown.hpp"
#include <cassert>
#include <iostream>
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    TTPRuntime::KnownDatabase db;
    db.Load(argv[1],{1,11,240,0});
    assert(db.Resolve(2230295)==0xC9D310);
    assert(db.Resolve(2248327)==0x1025E20);
    assert(db.Resolve(2248338)==0x1026940);
    std::cout << "AE240: " << db.Size() << " IDs\n";
    db.Load(argv[1],{1,11,221,0});
    assert(db.Resolve(2230295)==0xC9CF80);
    assert(db.Resolve(2248327)==0x1025A90);
    std::cout << "AE221: " << db.Size() << " IDs\n";
    db.Load(argv[1],{1,10,984,0});
    assert(db.Resolve(2230295)==0xC17340);
    std::cout << "NG984 database lookup only: " << db.Size() << " IDs\n";
    db.Load(argv[1],{1,10,163,0});
    std::cout << "OG163 database lookup only: " << db.Size() << " IDs\n";
    bool missing=false;
    try { (void)db.Resolve(UINT64_MAX); } catch(const std::exception&) {missing=true;}
    assert(missing);
    for(std::size_t n=0;n<112;++n) {
        bool rejected=false;
        try { db.Parse(std::vector<std::uint8_t>(n),{1,11,240,0}); }
        catch(const std::exception&) {rejected=true;}
        assert(rejected);
    }
    std::cout << "Missing ID and truncated-header rejection passed\n";
}
