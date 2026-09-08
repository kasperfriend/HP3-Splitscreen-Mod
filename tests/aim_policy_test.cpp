#include "../src/aim_policy.h"
#include <cassert>
#include <cstdio>
#include <limits>
static bool near(float a,float b) {return std::fabs(a-b)<0.001f;}
int main() {
    using namespace hp3aim;
    assert(samePackage("SpellCursor Save0.SpellCursor1","HPCam Save0.HPCam0"));
    assert(!samePackage("SpellCursor HP_preamble.SpellCursor0","HPCam Save0.HPCam0"));
    assert(!samePackage("SpellCursor Save00.SpellCursor1","HPCam Save0.HPCam0"));
    assert(!samePackage("Class hgame.SpellCursor","HPCam Save0.HPCam0"));
    assert(!samePackage("bad", "bad"));
    assert(near(range(770),770));assert(near(range(1200),1200));
    assert(near(range(-1),770));assert(near(range(0),770));
    assert(near(range(std::numeric_limits<float>::quiet_NaN()),770));
    assert(near(range(std::numeric_limits<float>::infinity()),770));
    float out[3];int zero[3]={0,0,0}; direction(zero,out);
    assert(near(out[0],1)&&near(out[1],0)&&near(out[2],0));
    int yaw[3]={0,16384,0};direction(yaw,out);
    assert(near(out[0],0)&&near(out[1],1));
    int pitch[3]={16384,0,0};direction(pitch,out);assert(near(out[2],1));
    float v[3]={10,20,30};rotate(v,zero,out);
    assert(near(out[0],10)&&near(out[1],20)&&near(out[2],30));
    rotate(v,yaw,out);assert(near(out[0],-20)&&near(out[1],10)&&near(out[2],30));
    rotate(v,pitch,out);assert(near(out[0],-30)&&near(out[1],20)&&near(out[2],10));
    int roll[3]={0,0,16384};rotate(v,roll,out);
    assert(near(out[0],10)&&near(out[1],30)&&near(out[2],-20));
    float cam[3]={-125,0,50},pawn[3]={0,0,0},dir[3]={1,0,0};
    assert(near(rayLength(cam,pawn,dir,770),895));
    // Pulling the camera against a wall must not shorten the pawn's range.
    cam[0]=-30;assert(near(rayLength(cam,pawn,dir,770),800));
    float centre[3]={400,100,80};gesturePoint(centre,dir,gestureDistance(60,1,0),out);
    assert(near(out[0],332)&&near(out[1],100)&&near(out[2],80));
    assert(near(gestureDistance(60,2,5),139));
    int object=1,clazz=2,other=3;
    assert(sameIdentity(&object,&object,&clazz,&clazz,"FX0","FX0"));
    assert(!sameIdentity(&object,&other,&clazz,&clazz,"FX0","FX0"));
    assert(!sameIdentity(&object,&object,&clazz,&other,"FX0","FX0"));
    assert(!sameIdentity(&object,&object,&clazz,&clazz,"FX0","FX1"));
    assert(!sameIdentity(nullptr,nullptr,&clazz,&clazz,"FX0","FX0"));
    puts("aim policy: geometry and identity assertions passed");
}
