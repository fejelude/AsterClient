#pragma once
// Minimal ABI descriptions inferred from the permitted SDK reference; no proprietary source.
#include "integration/shared.h"
#include <entt/entt.hpp>
enum class EntityId:std::uint32_t {};
template<>struct entt::internal::entt_traits<EntityId>{
    using value_type=EntityId;using entity_type=std::uint32_t;using version_type=std::uint16_t;
    static constexpr entity_type entity_mask=0x3FFFF,version_mask=0x7FF;
};
struct StateVectorComponent {aster::bridge::Float3 position,previous,velocity;};
struct ActorRotationComponent {aster::bridge::Float2 rotation,previous;};
struct AABBShapeComponent {aster::bridge::Float3 minimum,maximum;float width,height;};
struct ActorOwnerComponent {void* actor;};
struct ActorTypeComponent {std::int32_t type;};
struct RuntimeIDComponent {std::uint64_t id;};
struct IsDeadFlagComponent {};
struct MobHurtTimeComponent {std::int32_t hurtTime;};
namespace aster::bridge {
constexpr entt::id_type componentHash(const char* name){std::uint32_t hash=2166136261u;while(*name){hash^=static_cast<unsigned char>(*name++);hash*=16777619u;}return hash;}
}
#define ASTER_COMPONENT_ID(Type) template<>struct entt::type_hash<Type>{static constexpr entt::id_type value(){return aster::bridge::componentHash(#Type);}constexpr operator entt::id_type()const noexcept{return value();}};
ASTER_COMPONENT_ID(StateVectorComponent)
ASTER_COMPONENT_ID(ActorRotationComponent)
ASTER_COMPONENT_ID(AABBShapeComponent)
ASTER_COMPONENT_ID(ActorOwnerComponent)
ASTER_COMPONENT_ID(ActorTypeComponent)
ASTER_COMPONENT_ID(RuntimeIDComponent)
ASTER_COMPONENT_ID(IsDeadFlagComponent)
ASTER_COMPONENT_ID(MobHurtTimeComponent)
#undef ASTER_COMPONENT_ID
