#include "../src/LookSensitivity.hpp"
#include <cassert>
using namespace TrueThirdPerson::LookSensitivity;
int main() {
 assert(Parse(L"0.35",1)==0.35F);
 assert(Parse(L" 2.5 ",1)==2.5F);
 for (const auto* bad : {L"",L"nan",L"inf",L"0",L"-1",L"6",L"1junk",L"0,5"})
  assert(Parse(bad,0.5F)==0.5F);
 Profile controller{{1,2},{3,.5F},{4,5}};
 assert(controller.Select(false,false).y==2);
 assert(controller.Select(false,true).y==.5F);
 assert(controller.Select(true,true).x==4);
 assert(controller.Select(true,false).x==4); // ADS wins
 Profile mouse;
 assert(mouse.Select(false,true).y==1); // device profiles independent
}
