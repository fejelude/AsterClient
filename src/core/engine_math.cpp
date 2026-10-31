#include "core/engine_math.h"
#include <algorithm>
#include <cmath>
#include <numbers>
namespace aster {
Vec3 viewDirection(Orientation a){const auto k=std::numbers::pi/180.;return {-std::sin(a.yaw*k)*std::cos(a.pitch*k),-std::sin(a.pitch*k),std::cos(a.yaw*k)*std::cos(a.pitch*k)};}
double length(Vec3 v){return std::hypot(std::hypot(v.x,v.z),v.y);}
double nearestDistance(Vec3 p,const Box& b){return length({p.x-std::clamp(p.x,b.minimum.x,b.maximum.x),p.y-std::clamp(p.y,b.minimum.y,b.maximum.y),p.z-std::clamp(p.z,b.minimum.z,b.maximum.z)});}
std::optional<double> rayBox(Vec3 o,Vec3 d,const Box& b,double maximum){
    if(!std::isfinite(maximum)||maximum<0)return {};
    double lo=0,hi=maximum;
    const double origin[]={o.x,o.y,o.z},direction[]={d.x,d.y,d.z},minimum[]={b.minimum.x,b.minimum.y,b.minimum.z},maxima[]={b.maximum.x,b.maximum.y,b.maximum.z};
    for(unsigned axis=0;axis<3;++axis){
        if(!std::isfinite(origin[axis])||!std::isfinite(direction[axis])||!std::isfinite(minimum[axis])||!std::isfinite(maxima[axis])||minimum[axis]>maxima[axis])return {};
        if(std::abs(direction[axis])<1e-12){if(origin[axis]<minimum[axis]||origin[axis]>maxima[axis])return {};continue;}
        double a=(minimum[axis]-origin[axis])/direction[axis],z=(maxima[axis]-origin[axis])/direction[axis];if(a>z)std::swap(a,z);
        lo=std::max(lo,a);hi=std::min(hi,z);if(lo>hi)return {};
    }return lo;
}
Box targetingBox(const Box& original,double width){
    const auto x=(original.minimum.x+original.maximum.x)*.5,z=(original.minimum.z+original.maximum.z)*.5;
    const auto half=std::clamp(width,.1,5.)*.5;return {{x-half,original.minimum.y,z-half},{x+half,original.maximum.y,z+half}};
}
Vec3 scaledImpulse(Vec3 before,Vec3 incoming,double h,double v,bool combine,bool noBackwards,double yaw){
    if(combine)before={};
    Vec3 delta{(incoming.x-before.x)*std::clamp(h,0.,1.),(incoming.y-before.y)*std::clamp(v,0.,1.),(incoming.z-before.z)*std::clamp(h,0.,1.)};
    if(noBackwards){auto facing=viewDirection({yaw,0});auto dot=delta.x*facing.x+delta.z*facing.z;if(dot<0){delta.x-=dot*facing.x;delta.z-=dot*facing.z;}}
    return {before.x+delta.x,before.y+delta.y,before.z+delta.z};
}
Orientation aimCorrection(Orientation from,Orientation target,double smoothing,bool blatant,double dt,bool horizontal,bool vertical,double deadZone){
    const double yaw=horizontal?wrappedDegrees(target.yaw-from.yaw):0,pitch=vertical?target.pitch-from.pitch:0;
    const double error=std::hypot(yaw,pitch),inner=std::clamp(deadZone,0.,std::numbers::pi)*180./std::numbers::pi;
    if(error<=inner||dt<=0)return from;
    const double response=blatant?35.:14.;const double rate=blatant?1080.:240.;
    const double damping=1.+std::clamp(smoothing,0.,100.)*.08;
    double fraction=1.-std::exp(-response*std::min(dt,.1)/damping);
    fraction=std::min(fraction,(rate/damping)*std::min(dt,.1)/error);
    return {horizontal?wrappedDegrees(from.yaw+yaw*fraction):from.yaw,vertical?std::clamp(from.pitch+pitch*fraction,-90.,90.):from.pitch};
}
}
