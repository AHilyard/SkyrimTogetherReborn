#pragma once

struct TESObjectREFR;

/**
* @brief Dispatched when a local or remote actor's health changes locally.
* 
* The exact hueristics for what hit is registered by who can be found in 
* the Actor::HookDamageActor() hook.
*/
struct HealthChangeEvent
{
    HealthChangeEvent(uint32_t aHitteeId, uint32_t aHitterId, float aDeltaHealth)
        : HitteeId(aHitteeId), HitterId(aHitterId), DeltaHealth(aDeltaHealth)
    {
    }

    uint32_t HitteeId;
    uint32_t HitterId;
    float DeltaHealth;
};
