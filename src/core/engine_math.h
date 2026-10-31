#pragma once
#include "core/model.h"
#include <optional>
namespace aster {
struct Box {Vec3 minimum,maximum;};
Vec3 viewDirection(Orientation);
double length(Vec3);
double nearestDistance(Vec3,const Box&);
std::optional<double> rayBox(Vec3 origin,Vec3 direction,const Box&,double maximum);
Box targetingBox(const Box&,double fullWidth);
Vec3 scaledImpulse(Vec3 before,Vec3 incoming,double horizontal,double vertical,bool combine,bool noBackwards,double yaw);
Orientation aimCorrection(Orientation from,Orientation target,double smoothness,bool blatant,double seconds,bool horizontal,bool vertical,double deadZoneRadians);
}
