#include "cube/WorldDimensions.h"
// Per-tick world simulation moves creatures and projectiles and handles collisions with terrain and zone objects.
#include "cube/WorldBlocks.h"
#include "cube/Animation.h"
#include "cube/Noise.h"
#include "cube/BuffPacket.h"
#include "cube/BytesIO.h"
#include "cube/CombatBehavior.h"
#include "cube/CompanionBehavior.h"
#include "cube/LookAtPlayerBehavior.h"
#include "cube/RandomWalkBehavior.h"
#include "cube/RaceIds.h"
#include "cube/SequentialBehavior.h"
#include "cube/HitPacket.h"
#include "cube/Interaction.h"
#include "cube/Region.h"
#include "cube/ServerUpdates.h"
#include "cube/SoundIds.h"
#include "cube/StatCurves.h"
#include "cube/Spawn.h"
#include "cube/Zone.h"
#include "cube/Creature.h"
#include "cube/Sprite.h"
#include "cube/SpriteIds.h"
#include <math.h>

namespace cube {

// A zone's buildings for the clients: its coordinates and a copy of its building list.
ServerUpdates::BuildingListPacket::BuildingListPacket(const Zone &zone)
    : zone(zone.m_position),
      buildings(zone.m_buildings.begin(), zone.m_buildings.end())
{}

// Walks a box of the creature's size from `from` towards `to` in steps of at most one block (at
// most 50 blocks in all), each step one axis at a time: a step into the terrain or into a zone
// object is taken back, except that a horizontal step into the terrain may climb onto it when the
// space above is free. The way is straight when the box comes within `width` of `to` (and within
// the creature's height plus `width` vertically); otherwise the result is whether the box moved
// more than 25 blocks in all.
bool World::canWalkStraight(Creature *creature, const plasma::Vector<3, Fixed> &from,
                            const plasma::Vector<3, Fixed> &to, float width)
{
    plasma::Vector<2, float> offset(from[0] - to[0], from[1] - to[1]);
    if (offset.squaredLength() < width * width &&
        fabs((float)(from[2] - to[2])) < creature->m_entityData.appearance.size[2] + width)
        return true;

    plasma::Vector<3, Fixed> position = from;
    plasma::Vector<3, float> direction(to[0] - position[0], to[1] - position[1], 0.0f);
    if (direction.squaredLength() > 2500.0f) {
        direction.normalize();
        direction *= 50.0f;
    }
    // The number of steps: the length with a vertical part of 0.75, plus one.
    int steps = (int)(plasma::Vector<3, float>(direction[0], direction[1], 0.75f).length() + 1.0f);
    for (int i = 0; i < steps; i++) {
        // Arrived: within width horizontally (this time inclusive).
        plasma::Vector<2, float> remaining(plasma::Vector<2, Fixed>((position - to)[0], (position - to)[1]));
        if (remaining.squaredLength() <= width * width &&
            fabs((float)(position[2] - to[2])) < creature->m_entityData.appearance.size[2] + width)
            return true;

        // One block towards the target, and down.
        plasma::Vector<2, float> flat(plasma::Vector<2, Fixed>((to - position)[0], (to - position)[1]));
        plasma::Vector<3, float> step(flat[0], flat[1], 0.0f);
        if (step.squaredLength() > 0.0f)
            step.normalize();
        step[2] = -0.71f;

        for (int axis = 0; axis < 3; axis++) {
            Fixed move(step[axis]);
            position[axis] += move;
            bool climb = true;
            bool blocked = isBlocked(position, creature->m_entityData.appearance.size, false);
            if (!blocked || axis == 2) {
                // The objects of the 3 x 3 cells around the position.
                int cellX = (int)position[0] / BlocksPerObjectCell;
                int cellY = (int)position[1] / BlocksPerObjectCell;
                for (int x = cellX - 1; x <= cellX + 1; x++) {
                    for (int y = cellY - 1; y <= cellY + 1; y++) {
                        std::list<Zone::CellRecord> *objects = getCellObjects(x, y);
                        if (objects) {
                            for (std::list<Zone::CellRecord>::iterator it = objects->begin(); it != objects->end();
                                 ++it) {
                                Zone::Object *object = it->object;
                                int type = object->type;
                                if (type == StaticObjectSpikeTrap || type == StaticObjectFireTrap ||
                                    type == StaticObjectLever)
                                    continue;
                                if (creature->m_overlappedObjects.find(object) != creature->m_overlappedObjects.end())
                                    continue;
                                if ((type == StaticObjectDoor || type == StaticObjectStompTrap ||
                                     type == StaticObjectBigDoor || type == StaticObjectWindow ||
                                     type == StaticObjectGate) &&
                                    !object->active)
                                    continue;
                                plasma::Vector<3, float> size = object->size;
                                if (object->rotation % 2 != 0) {
                                    float swap = size[0];
                                    size[0] = size[1];
                                    size[1] = swap;
                                }
                                // Only objects ahead along the axis.
                                if ((position[axis] - object->position[axis]) * step[axis] >= 0.0f)
                                    continue;
                                const plasma::Vector<3, float> &own = creature->m_entityData.appearance.size;
                                if (position[0] + Fixed(own[0] * 0.5f) < object->position[0] - Fixed(size[0] * 0.5f) ||
                                    position[0] - Fixed(own[0] * 0.5f) >= object->position[0] + Fixed(size[0] * 0.5f))
                                    continue;
                                if (position[1] + Fixed(own[1] * 0.5f) < object->position[1] - Fixed(size[1] * 0.5f) ||
                                    position[1] - Fixed(own[1] * 0.5f) >= object->position[1] + Fixed(size[1] * 0.5f))
                                    continue;
                                if (position[2] + Fixed(own[2] * 0.5f) < object->position[2])
                                    continue;
                                if (position[2] - Fixed(own[2] * 0.5f) < object->position[2] + Fixed(size[2])) {
                                    blocked = true;
                                    break;
                                }
                            }
                        }
                        if (blocked)
                            break;
                    }
                    if (blocked)
                        break;
                }
                climb = false;
            }
            if (!blocked)
                continue;
            if (axis != 2 && climb &&
                !(creature->m_entityData.appearance.appearanceFlags & (1 << AppearanceFlagFish))) {
                // Climb when the box raised by 1.01 blocks is free.
                plasma::Vector<3, float> size = creature->m_entityData.appearance.size;
                plasma::Vector<3, int> low =
                    toBlock(position + plasma::Vector<3, Fixed>(plasma::Vector<3, float>(0.0f, 0.0f, 1.01f)) -
                            plasma::Vector<3, Fixed>(0.5f * size));
                plasma::Vector<3, int> high =
                    toBlock(position + plasma::Vector<3, Fixed>(plasma::Vector<3, float>(0.0f, 0.0f, 1.01f)) +
                            plasma::Vector<3, Fixed>(0.5f * size));
                bool solid = false;
                for (int x = low[0]; x <= high[0] && !solid; x++) {
                    for (int y = low[1]; y <= high[1] && !solid; y++) {
                        for (int z = low[2]; z <= high[2]; z++) {
                            if (getBlock(x, y, z, 0).isSolid())
                                solid = true;
                        }
                    }
                }
                if (!solid) {
                    Fixed halfHeight(creature->m_entityData.appearance.size[2] * 0.5f);
                    Fixed bottom = position[2] - halfHeight;
                    if (bottom.raw < 0)
                        bottom -= Fixed(1);
                    position[2] = Fixed((int)bottom) - Fixed(-1.01) + halfHeight;
                    position[2] += Fixed(1.01f);
                    continue;
                }
            }
            position[axis] -= move;
        }
    }
    plasma::Vector<3, float> moved(position - from);
    return moved.squaredLength() > 625.0f;
}

void World::resolveCollisions(Creature *creature)
{
    if (creature->m_entityData.hp < 0.0f)
        return;

    // A creature inside the terrain is lifted onto the block its bottom is in (plus 1.1 blocks).
    for (int attempt = 0; attempt < 1; attempt++) {
        plasma::Vector<3, float> size = creature->m_entityData.appearance.size;
        plasma::Vector<3, Fixed> position = creature->m_entityData.position;
        plasma::Vector<3, int> low = toBlock(position - plasma::Vector<3, Fixed>(0.5f * size));
        plasma::Vector<3, int> high = toBlock(position + plasma::Vector<3, Fixed>(0.5f * size));
        bool solid = false;
        for (int x = low[0]; x <= high[0]; x++) {
            for (int y = low[1]; y <= high[1]; y++) {
                for (int z = low[2]; z <= high[2]; z++) {
                    // Only loaded zones count (missing terrain would be solid).
                    if (this->zone(x / BlocksPerZone, y / BlocksPerZone) && getBlock(x, y, z, 0).isSolid())
                        solid = true;
                }
                if (solid)
                    break;
            }
            if (solid)
                break;
        }
        if (!solid)
            break;
        Fixed halfHeight(creature->m_entityData.appearance.size[2] * 0.5f);
        creature->m_entityData.position[2] =
            Fixed(toBlock(creature->m_entityData.position[2] - halfHeight)) + halfHeight;
        creature->m_entityData.position[2] += Fixed(1.1f);
        creature->m_stepOffset = 0;
    }

    // The airships: the creature's box in the airship sprite's voxels.
    for (std::map<long long, Zone::Airship *>::iterator it = m_airships.begin(); it != m_airships.end(); ++it) {
        Zone::Airship *&airship = it->second;
        if (!airship)
            continue;
        plasma::Vector<3, Fixed> position = creature->m_entityData.position;
        if (plasma::Vector<3, float>(position - airship->currentPosition).squaredLength() > 4096.0f)
            continue;
        Sprite *sprite = m_spriteManager.sprite(SpriteAirship);
        if (!sprite)
            continue;

        // From the world into the sprite: turned back about the airship's position, which is the sprite's
        // centre.
        plasma::Matrix<float> toSprite;
        toSprite.identity();
        toSprite.translate(sprite->sizeX() * 0.5f, sprite->sizeY() * 0.5f, sprite->sizeZ() * 0.5f);
        toSprite.rotateZ(-airship->currentRotation);
        toSprite.translate(plasma::Vector<3, float>(position - airship->currentPosition));
        plasma::Vector<3, float> local = toSprite * plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);

        plasma::Vector<3, float> size = creature->m_entityData.appearance.size;
        plasma::Vector<3, int> low = toBlock(plasma::Vector<3, Fixed>(local - 0.5f * size));
        plasma::Vector<3, int> high = toBlock(plasma::Vector<3, Fixed>(local + 0.5f * size));
        bool overlaps = false;
        for (int x = low[0]; x <= high[0]; x++) {
            for (int y = low[1]; y <= high[1]; y++) {
                for (int z = low[2]; z <= high[2]; z++) {
                    if (sprite->voxel(x, y, z) != plasma::Vector<3, unsigned char>(0, 0, 0))
                        overlaps = true;
                }
                if (overlaps)
                    break;
            }
            if (overlaps)
                break;
        }
        if (!overlaps)
            continue;

        // The nearest of the four places one voxel aside (+x, -x, +y, -y) where the box is free, 0.05
        // voxels clear of the voxels it overlaps.
        float nearest = -1.0f;
        plasma::Vector<3, float> target;
        bool free = true;
        for (int x = low[0] + 1; x <= high[0] + 1; x++) {
            for (int y = low[1]; y <= high[1]; y++) {
                for (int z = low[2]; z <= high[2]; z++) {
                    if (sprite->voxel(x, y, z) != plasma::Vector<3, unsigned char>(0, 0, 0))
                        free = false;
                }
                if (!free)
                    break;
            }
            if (!free)
                break;
        }
        if (free) {
            plasma::Vector<3, float> place(low[0] + 1.05f + size[0] * 0.5f, local[1], local[2]);
            float distance = (place - local).squaredLength();
            if (nearest < 0.0f || distance < nearest) {
                target = place;
                nearest = distance;
            }
        }
        free = true;
        for (int x = low[0] - 1; x <= high[0] - 1; x++) {
            for (int y = low[1]; y <= high[1]; y++) {
                for (int z = low[2]; z <= high[2]; z++) {
                    if (sprite->voxel(x, y, z) != plasma::Vector<3, unsigned char>(0, 0, 0))
                        free = false;
                }
                if (!free)
                    break;
            }
            if (!free)
                break;
        }
        if (free) {
            plasma::Vector<3, float> place(high[0] - 0.05f - size[0] * 0.5f, local[1], local[2]);
            float distance = (place - local).squaredLength();
            if (nearest < 0.0f || distance < nearest) {
                target = place;
                nearest = distance;
            }
        }
        free = true;
        for (int x = low[0]; x <= high[0]; x++) {
            for (int y = low[1] + 1; y <= high[1] + 1; y++) {
                for (int z = low[2]; z <= high[2]; z++) {
                    if (sprite->voxel(x, y, z) != plasma::Vector<3, unsigned char>(0, 0, 0))
                        free = false;
                }
                if (!free)
                    break;
            }
            if (!free)
                break;
        }
        if (free) {
            plasma::Vector<3, float> place(local[0], low[1] + 1.05f + size[1] * 0.5f, local[2]);
            float distance = (place - local).squaredLength();
            if (nearest < 0.0f || distance < nearest) {
                target = place;
                nearest = distance;
            }
        }
        free = true;
        for (int x = low[0]; x <= high[0]; x++) {
            for (int y = low[1] - 1; y <= high[1] - 1; y++) {
                for (int z = low[2]; z <= high[2]; z++) {
                    if (sprite->voxel(x, y, z) != plasma::Vector<3, unsigned char>(0, 0, 0))
                        free = false;
                }
                if (!free)
                    break;
            }
            if (!free)
                break;
        }
        if (free) {
            plasma::Vector<3, float> place(local[0], high[1] - 0.05f - size[1] * 0.5f, local[2]);
            float distance = (place - local).squaredLength();
            if (nearest < 0.0f || distance < nearest) {
                target = place;
                nearest = distance;
            }
        }
        if (nearest < 0.0f)
            continue;

        // The move, turned back into the world.
        plasma::Matrix<float> toWorld;
        toWorld.identity();
        toWorld.rotateZ(airship->currentRotation);
        creature->m_entityData.position += plasma::Vector<3, Fixed>(toWorld.transformDirection(target - local));
    }
}

// Advances the world by elapsed milliseconds: updates clocks, incoming hits and buffs, nearby zone objects and items, spawn schedules, despawning, companions, deaths, spawning, creatures and projectiles. Queues outgoing events for clients.
namespace {
enum {
    DismissedPetDeathAgeMs = 5000
};
enum {
    BombSmokeParticleCount = 20
};
enum {
    BlockDebrisParticleCount = 3
};
enum {
    LightRecomputeMargin = 8
};
enum {
    BombCriticalChanceDenominator = 4
};
enum {
    DungeonLootFirstRarityChanceDenominator = 20
};
enum {
    DungeonLootSecondRarityChanceDenominator = 100
};
enum {
    SpellChannelSoundPeriodMs = 200
};
enum {
    ShockwaveParticleCount = 80
};
enum {
    CloseSpellParticleCount = 20
};
enum {
    HeadbuttHitStartDivisor = 3
};
enum {
    Ability93HitStartDivisor = 5
};
enum {
    SwirlStrikeAndPerforateHitsPerStrike = 3
};
enum {
    CloseSwirlHitsPerStrike = 6
};
enum {
    VortexHitsPerStrike = 12
};
enum {
    MeleeTerrainBreakRadiusCap = 8
};
enum {
    InstantFireBuffDurationMs = 30000
};
enum {
    ManaBonusChanceDenominator = 8
};
enum {
    TeleportSearchHorizontalRadius = 4
};
enum {
    TeleportSearchMaximumHeight = 6
};
enum {
    RayMissParticleCount = 5
};
enum {
    TamenessBuffDurationMs = 5000
};
enum {
    ConsumptionSoundPeriodMs = 400
};
enum {
    RetreatRushDurationMs = 500
};
enum {
    RetreatBackpedalExemptionMs = 1000
};
enum {
    WaterSplashParticleCount = 15
};
enum {
    RollSoundRemainingTimeMs = 500
};
enum {
    BoomerangLifetimeMs = 1200
};
enum {
    BoomerangReturnStartMs = 800
};
enum {
    BoomerangDamageStartMs = 300
};
enum {
    BoomerangSwingSoundPeriodMs = 200
};
enum {
    ArrowDustParticleCount = 4
};
enum {
    UnpoweredWaterCloudInitialAgeMs = 3000
};
enum {
    QuakePulseMs = 1000
};
enum {
    DefaultRepeatingStrikePulseMs = 250
};
enum {
    ForwardLeapStrikeWindowNumerator = 3
};
enum {
    ForwardLeapStrikeWindowDenominator = 4
};
enum {
    BoomerangFinalReturnStartMs = 1100
};
enum {
    SleepClockAcceleration = 100
};
enum {
    WorldClockRate = 10
};
}

void World::update(int milliseconds, ServerUpdates *outgoing, ServerUpdates *incoming)
{
    // Sleeping players regenerate 5 % of their maximum hp per second; while anyone sleeps the time of
    // day runs ten times faster.
    bool sleeping = false;
    for (std::map<long long, Creature *>::iterator it = m_creatures.begin(); it != m_creatures.end(); ++it) {
        if (it->second && it->second->m_entityData.hostilityType == HostilityPlayer &&
            it->second->m_entityData.currentAbility == AbilitySleep && it->second->m_entityData.hp > 0.0f) {
            if (it->second == m_localPlayer) {
                m_localPlayer->m_entityData.hp += m_localPlayer->maxHP() * ((float)milliseconds * 0.001f * 0.05f);
                if (m_localPlayer->m_entityData.hp > m_localPlayer->maxHP())
                    m_localPlayer->m_entityData.hp = m_localPlayer->maxHP();
            }
            sleeping = true;
        }
    }
    m_clock += milliseconds;
    m_time += sleeping ? milliseconds * SleepClockAcceleration : milliseconds * WorldClockRate;
    // A world without a name (the start menu's) stays at nine in the morning.
    if (m_name.size() == 0)
        m_time = 9 * MillisecondsPerHour;
    while (m_time > 24 * MillisecondsPerHour) {
        m_day++;
        m_time -= 24 * MillisecondsPerHour;
        clearRegionPopulationState(outgoing);
    }
    // Every ten seconds of the clock the day and the time are saved.
    if ((m_clock + milliseconds) / 10000 != m_clock / 10000 && m_database.isOpen() && m_name.size() != 0) {
        BytesIO time;
        time.write(&m_day, 4);
        time.write(&m_time, 4);
        m_database.write(std::string("time"), time);
    }

    float elapsed = (float)milliseconds;
    float seconds = elapsed * 0.001f;
    std::list<Creature *> players;
    std::set<Zone *> playerZones;
    // A level-2 pet egg (pet item 24) hatches (25).
    if (m_localPlayer && m_localPlayer->m_entityData.equipment.pet.category == ItemCategoryPet &&
        m_localPlayer->m_entityData.equipment.pet.id == PetItemEgg &&
        m_localPlayer->m_entityData.equipment.pet.level > 1)
        m_localPlayer->m_entityData.equipment.pet.id = PetItemTurtle;

    for (std::map<long long, Creature *>::iterator it = m_creatures.begin(); it != m_creatures.end(); ++it) {
        if (!it->second)
            continue;
        // A friendly character with an empty inventory takes its spawn's (spawnSlot: zone x, zone y and
        // spawn index).
        if (it->second->m_entityData.hostilityType == HostilityFriendlyNpc &&
            it->second->m_inventory.m_inventoryTabs.empty()) {
            plasma::Vector<3, int> &home = it->second->m_entityData.spawnSlot;
            Zone *zone = this->zone(home[0], home[1]);
            if (zone && home[2] >= 0 && home[2] < (int)zone->m_spawns.size())
                it->second->m_inventory = zone->m_spawns[home[2]]->inventory;
        }
        Creature *&creature = it->second;
        // A mission resident dies when nobody fights it and its mission (the zone square's record) is
        // completed.
        if ((creature->m_entityData.appearance.appearanceFlags & (1 << AppearanceFlagMissionResident)) &&
            creature->m_aggro.empty()) {
            Region::Record *record = getRecord(creature->m_entityData.spawnSlot[0] / ZonesPerSceneryRecord,
                                               creature->m_entityData.spawnSlot[1] / ZonesPerSceneryRecord);
            if (record && record->mission.kind != MissionNone && record->mission.progress == MissionCompleted) {
                creature->m_entityData.hp = 0.0f;
                creature->m_entityData.timeSinceAbility = 0;
            }
        }
        if (creature->m_entityData.hostilityType != HostilityPlayer)
            continue;
        players.push_back(creature);
        // The zones around each player stay active.
        int zoneX = (int)creature->m_entityData.position[0] / BlocksPerZone;
        int zoneY = (int)creature->m_entityData.position[1] / BlocksPerZone;
        for (int x = zoneX - 1; x <= zoneX + 1; x++) {
            for (int y = zoneY - 1; y <= zoneY + 1; y++) {
                Zone *zone = this->zone(x, y);
                if (zone)
                    playerZones.insert(zone);
            }
        }
        if (m_client)
            continue;
        // The player's region is populated once; a mission of the player's zone square starts when the
        // player comes within its influence.
        plasma::Vector<2, int> region =
            findNearestRegion((int)creature->m_entityData.position[0], (int)creature->m_entityData.position[1]);
        Region *loaded = getRegion(region[0], region[1]);
        if (loaded && !loaded->m_populated)
            populateRegion(region[0], region[1], outgoing);
        plasma::Vector<2, int> square((int)creature->m_entityData.position[0] / BlocksPerZone / 8,
                                      (int)creature->m_entityData.position[1] / BlocksPerZone / 8);
        Region::Record *record = getRecord(square[0], square[1]);
        if (record && record->mission.kind != MissionNone && record->mission.progress == MissionNotStarted &&
            record->influence(creature->m_entityData.position[0], creature->m_entityData.position[1]) > 0.0f) {
            record->mission.progress = MissionInProgress;
            ServerUpdates::MissionPacket packet;
            packet.position = square;
            packet.mission = record->mission;
            outgoing->missions.push_back(packet);
        }
    }

    // The clients' hits and buffs. While the world saves nothing (a client's copy) the outgoing lists
    // are cleared first and last.
    std::set<Zone *> changedZones;
    // The zones whose buildings changed (the build interaction).
    std::set<Zone *> buildingZones;
    if (incoming) {
        if (!incoming->hits.empty()) {
            if (m_client)
                outgoing->hits.clear();
            for (std::list<HitPacket>::iterator hit = incoming->hits.begin(); hit != incoming->hits.end(); hit++)
                applyHit(*hit, outgoing, &changedZones);
        }
        std::list<BuffPacket> &buffs = incoming->buffs;
        if (!buffs.empty()) {
            for (std::list<BuffPacket>::iterator buff = buffs.begin(); buff != buffs.end(); buff++) {
                if (creatureByID(buff->targetId))
                    creatureByID(buff->targetId)->setBuff(buff->buff);
            }
            if (m_client)
                outgoing->buffs.clear();
        }
    }

    // The items and objects of the zones around the players.
    for (std::set<Zone *>::iterator it = playerZones.begin(); it != playerZones.end(); it++) {
        std::list<std::vector<Zone::ZoneItem>::iterator> removedItems;
        Zone *zone = *it;
        for (std::vector<Zone::ZoneItem>::iterator item = zone->m_items.begin(); item != zone->m_items.end(); item++) {
            Zone::ZoneItem &dropped = *item;
            // A wall item (category 16) clings to the first solid block beside it and faces away from it.
            if (dropped.item.category == ItemCategoryPainting) {
                plasma::Vector<3, Fixed> &position = dropped.position;
                Fixed &z = position[2];
                Fixed &y = position[1];
                Fixed &x = position[0];
                if (getBlock(x, y - 1, z, 0).isSolid()) {
                    y = Fixed((int)y) + 0.2f;
                    dropped.rotation = 180.0f;
                }
                if (getBlock(x, y + 1, z, 0).isSolid()) {
                    y = Fixed((int)y + 1) - 0.2f;
                    dropped.rotation = 0.0f;
                }
                if (getBlock(x - 1, y, z, 0).isSolid()) {
                    x = Fixed((int)x) + 0.2f;
                    dropped.rotation = 90.0f;
                }
                if (getBlock(x + 1, y, z, 0).isSolid()) {
                    x = Fixed((int)x + 1) - 0.2f;
                    dropped.rotation = 270.0f;
                }
            }
            if (dropped.hiddenTimeRemaining > 0) {
                dropped.hiddenTimeRemaining -= milliseconds;
                if (dropped.hiddenTimeRemaining < 0)
                    dropped.hiddenTimeRemaining = 0;
            }
            // The item can be picked up again: a sound tells so.
            if (dropped.dropTime > 0) {
                dropped.dropTime -= milliseconds;
                if (dropped.dropTime <= 0) {
                    dropped.dropTime = 0;
                    if (!m_client) {
                        ServerUpdates::SoundPacket sound;
                        sound.position = dropped.position;
                        sound.volume = 1.0f;
                        sound.pitch = (float)rand() * 0.1f / float(RAND_MAX) + 1.0f;
                        sound.sound = dropped.item.category == ItemCategoryCoin ? SoundDropCoin : SoundDropItem;
                        outgoing->sounds.push_back(sound);
                    }
                }
            }
        }
        for (std::list<std::vector<Zone::ZoneItem>::iterator>::reverse_iterator removed = removedItems.rbegin();
             removed != removedItems.rend(); removed++)
            zone->m_items.erase(*removed);

        int index = 0;
        for (std::vector<Zone::Object>::iterator it2 = zone->m_objects.begin(); it2 != zone->m_objects.end();
             index++, it2++) {
            Zone::Object &object = *it2;
            object.transitionTime += milliseconds;
            if (object.occupantId != 0) {
                Creature *occupant = creatureByID(object.occupantId);
                bool release = !occupant || occupant->m_entityData.stunTime > 0;
                if (!release) {
                    // A seat holds its occupant sitting, a bed lying, on top of the object and facing its way.
                    switch (object.type) {
                    case StaticObjectStool:
                    case StaticObjectBench:
                        occupant->m_entityData.position =
                            object.position +
                            plasma::Vector<3, Fixed>(plasma::Vector<3, float>(
                                0.0f, 0.0f, occupant->m_entityData.appearance.size[2] * 0.5f + object.size[2]));
                        occupant->m_entityData.velocity = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
                        occupant->m_entityData.rotation[2] = (float)((object.rotation + 2) * 90);
                        occupant->m_entityData.currentAbility = AbilitySit;
                        break;
                    case StaticObjectBed:
                    case StaticObjectBeachTowel:
                    case StaticObjectSleepingMat:
                        occupant->m_entityData.position =
                            object.position +
                            plasma::Vector<3, Fixed>(plasma::Vector<3, float>(
                                0.0f, 0.0f, occupant->m_entityData.appearance.size[2] * 0.5f + object.size[2]));
                        occupant->m_entityData.velocity = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
                        occupant->m_entityData.rotation[2] = (float)(object.rotation * 90);
                        occupant->m_entityData.currentAbility = AbilitySleep;
                        break;
                    }
                    // It stays while it rests within four blocks.
                    release =
                        plasma::Vector<3, float>(occupant->m_entityData.position - object.position).squaredLength() >
                            16.0f ||
                        occupant->m_entityData.acceleration.squaredLength() > 0.0f ||
                        (occupant->m_entityData.currentAbility != AbilitySit &&
                         occupant->m_entityData.currentAbility != AbilitySleep);
                    if (!release) {
                        switch (object.type) {
                        case StaticObjectStool:
                        case StaticObjectBench:
                            occupant->m_entityData.position =
                                object.position +
                                plasma::Vector<3, Fixed>(plasma::Vector<3, float>(
                                    0.0f, 0.0f, occupant->m_entityData.appearance.size[2] * 0.5f + object.size[2]));
                            occupant->m_entityData.velocity = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
                            occupant->m_entityData.rotation[2] = (float)((object.rotation + 2) * 90);
                            break;
                        case StaticObjectBed:
                        case StaticObjectBeachTowel:
                        case StaticObjectSleepingMat:
                            occupant->m_entityData.position =
                                object.position +
                                plasma::Vector<3, Fixed>(plasma::Vector<3, float>(
                                    0.0f, 0.0f, occupant->m_entityData.appearance.size[2] * 0.5f + object.size[2]));
                            occupant->m_entityData.velocity = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
                            occupant->m_entityData.rotation[2] = (float)(object.rotation * 90);
                            break;
                        }
                    }
                }
                if (release) {
                    if (m_client)
                        continue;
                    object.occupantId = 0;
                    outgoing->objects.push_back(ServerUpdates::ZoneObjectPacket(
                        plasma::Vector<3, int>(zone->m_position[0], zone->m_position[1], index), object));
                }
            }
            if (m_client)
                continue;
            int type = object.type;
            if (type != StaticObjectFireTrap && type != StaticObjectSpikeTrap && type != StaticObjectStompTrap)
                continue;
            // A door opens when a player comes within 30 blocks and closes two seconds after it last changed.
            if ((type == StaticObjectFireTrap || type == StaticObjectStompTrap || type == StaticObjectSpikeTrap) &&
                object.transitionTime > 2000) {
                if (object.active) {
                    setObjectActive(&*it2, outgoing, false);
                    outgoing->objects.push_back(ServerUpdates::ZoneObjectPacket(
                        plasma::Vector<3, int>(zone->m_position[0], zone->m_position[1], index), object));
                } else {
                    for (std::map<long long, Creature *>::iterator player = m_creatures.begin();
                         player != m_creatures.end(); ++player) {
                        Creature *creature = player->second;
                        if (creature && creature->m_entityData.hostilityType == HostilityPlayer &&
                            (object.position - creature->m_entityData.position).squaredLength() < 900) {
                            setObjectActive(&*it2, outgoing, !object.active);
                            outgoing->objects.push_back(ServerUpdates::ZoneObjectPacket(
                                plasma::Vector<3, int>(zone->m_position[0], zone->m_position[1], index), object));
                            break;
                        }
                    }
                }
            }
            // An open trap hurts every creature but enemies within five blocks: doors and gates every 200 ms
            // of the clock, a type-8 trap once when it opens, five times as hard and twice as far.
            if (object.active && (((object.type == StaticObjectFireTrap || object.type == StaticObjectSpikeTrap) &&
                                   (m_clock + milliseconds) / 200 != m_clock / 200) ||
                                  (object.type == StaticObjectStompTrap && object.transitionTime == 0))) {
                for (std::map<long long, Creature *>::iterator victim = m_creatures.begin();
                     victim != m_creatures.end(); victim++) {
                    Creature *&creature = victim->second;
                    if (!creature)
                        continue;
                    plasma::Vector<3, Fixed> &position = object.position;
                    if (!((position - creature->m_entityData.position).squaredLength() < 25) ||
                        creature->m_entityData.hostilityType == HostilityEnemy)
                        continue;
                    plasma::Vector<3, float> push(creature->m_entityData.position - position);
                    float &up = push[2];
                    up = 0.0f;
                    if (push.squaredLength() > 0.0f)
                        push.normalize();
                    push *= 20.0f;
                    up = 5.0f;
                    if (object.type == StaticObjectStompTrap)
                        push *= 2.0f;
                    // powf here and below is VC11's out-of-line (float)pow((double)x, (double)y).
                    float damage = powf(2.0f, (float)(levelAt(position[0], position[1]) - 1) * 0.25f) * 10.0f;
                    if (object.type == StaticObjectStompTrap)
                        damage *= 5.0f;
                    attackCreature(creature, 0, damage, false, false, 0.0f, position, push, outgoing, &changedZones,
                                   object.type == StaticObjectFireTrap, 0, 0, true);
                }
            }
        }
    }

    if (m_client) {
        // A client's copy only links each pet to its owner.
        for (std::map<long long, Creature *>::iterator it = m_creatures.begin(); it != m_creatures.end(); it++) {
            if (it->second && it->second->m_entityData.hostilityType == HostilityPet &&
                creatureByID(it->second->m_entityData.ownerId))
                creatureByID(it->second->m_entityData.ownerId)->m_companionId = it->second->m_id;
        }
    } else {
        // The spawns of the active zones move to the latest place of their daily schedule that is due.
        for (std::set<Zone *>::iterator it = playerZones.begin(); it != playerZones.end(); it++) {
            for (int i = 0; i < (int)(*it)->m_spawns.size(); i++) {
                Spawn *spawn = (*it)->m_spawns[i];
                std::vector<Creature::ScheduleEntry> &schedule = spawn->schedule;
                for (int j = 0; j < (int)schedule.size(); j++) {
                    if (getTime() >= schedule[j].time)
                        spawn->position = schedule[j].position;
                }
                spawn->respawnTimeRemaining -= milliseconds;
            }
        }

        // Creatures that are neither players nor pets despawn beyond their despawn distance
        // (m_despawnDistance) plus 20 blocks from every player, unless they fight a player within 512
        // blocks of their origin.
        std::list<std::map<long long, Creature *>::iterator> despawned;
        for (std::map<long long, Creature *>::iterator it = m_creatures.begin(); it != m_creatures.end(); it++) {
            Creature *&creature = it->second;
            if (!creature || creature->m_entityData.hostilityType == HostilityPlayer ||
                creature->m_entityData.hostilityType == HostilityPet || !(creature->m_entityData.hp > 0.0f))
                continue;
            bool fighting = false;
            for (std::map<long long, float>::iterator aggro = creature->m_aggro.begin();
                 aggro != creature->m_aggro.end(); aggro++) {
                if (aggro->first != 0 && creatureByID(aggro->first) &&
                    creatureByID(aggro->first)->m_entityData.hostilityType == HostilityPlayer && aggro->second > 0.0f &&
                    plasma::Vector<3, float>(creature->m_entityData.position - creature->m_entityData.origin)
                            .squaredLength() < 262144.0f) {
                    fighting = true;
                    break;
                }
            }
            if (fighting)
                continue;
            float nearest = -1.0f;
            for (std::list<Creature *>::iterator player = players.begin(); player != players.end(); player++) {
                float distance =
                    plasma::Vector<3, float>(creature->m_entityData.position - (*player)->m_entityData.position)
                        .squaredLength();
                if (nearest < 0.0f || distance < nearest)
                    nearest = distance;
            }
            if (nearest > (creature->m_despawnDistance + 20.0f) * (creature->m_despawnDistance + 20.0f)) {
                despawned.push_back(it);
                for (std::map<long long, Creature *>::iterator other = m_creatures.begin(); other != m_creatures.end();
                     other++) {
                    if (other->second)
                        other->second->m_aggro.erase(creature->m_id);
                }
            }
        }
        for (std::list<std::map<long long, Creature *>::iterator>::iterator it = despawned.begin();
             it != despawned.end(); it++) {
            std::map<long long, Creature *>::iterator &entry = *it;
            if (entry->second)
                delete entry->second;
            m_creatures.erase(entry);
        }

        // A player's equipped pet item (category 19) keeps a companion creature of its race, level,
        // experience and name; the companion is rebuilt when the item or the player's first skill changes.
        if (!m_name.empty()) {
            for (std::list<Creature *>::iterator it = players.begin(); it != players.end(); it++) {
                Creature *&player = *it;
                if (player->m_entityData.equipment.pet.category == ItemCategoryPet) {
                    if (player->m_companionId == 0 || !creatureByID(player->m_companionId)) {
                        player->m_companionId = 0;
                        for (std::map<long long, Creature *>::iterator pet = m_creatures.begin();
                             pet != m_creatures.end(); pet++) {
                            if (pet->second && pet->second->m_entityData.hostilityType == HostilityPet &&
                                pet->second->m_entityData.ownerId == player->m_id) {
                                player->m_companionId = pet->second->m_id;
                                break;
                            }
                        }
                    }
                    bool rebuilt = false;
                    Creature *companion;
                    if (player->m_companionId != 0 && (companion = creatureByID(player->m_companionId)) != 0 &&
                        (player->m_lastPetSkillLevel != player->m_entityData.skillLevels[SkillPetTaming] ||
                         player->m_entityData.equipment.pet != player->m_lastPetItem)) {
                        rebuilt = true;
                        companion->reset();
                        int &race = companion->m_entityData.race;
                        race = player->m_entityData.equipment.pet.id;
                        companion->m_entityData.level = player->m_entityData.equipment.pet.level;
                        companion->m_entityData.xp = player->m_entityData.equipment.pet.modifier;
                        // The pet's name is kept in its item's spirit materials.
                        if (player->m_entityData.equipment.pet.spiritCount < 16) {
                            for (int i = 0; i < player->m_entityData.equipment.pet.spiritCount; i++)
                                companion->m_entityData.name[i] =
                                    player->m_entityData.equipment.pet.spirits[i].material;
                            companion->m_entityData.name[player->m_entityData.equipment.pet.spiritCount] = 0;
                        }
                        Creature::Appearance &appearance = companion->m_entityData.appearance;
                        companion->m_entityData.ownerId = player->m_id;
                        appearance = Creature::Appearance();
                        fillAppearance(race, appearance, 0);
                        initCompanion(player->m_id, companion);
                    }
                    player->m_lastPetSkillLevel = player->m_entityData.skillLevels[SkillPetTaming];
                    player->m_lastPetItem = player->m_entityData.equipment.pet;
                    if (rebuilt || player->m_companionId != 0 || (player->m_entityData.binaryToggles.gliding))
                        continue;
                    // A new companion takes the id below the lowest one.
                    long long id = 0;
                    for (std::map<long long, Creature *>::iterator other = m_creatures.begin();
                         other != m_creatures.end(); other++) {
                        if (other->second && other->first < id)
                            id = other->first;
                    }
                    id--;
                    player->m_companionId = id;
                    Creature *pet = new Creature(id);
                    m_creatures[id] = pet;
                    pet->reset();
                    pet->m_entityData.ownerId = player->m_id;
                    pet->m_entityData.hostilityType = HostilityPet;
                    pet->m_entityData.race = player->m_entityData.equipment.pet.id;
                    pet->m_entityData.level = player->m_entityData.equipment.pet.level;
                    pet->m_entityData.xp = player->m_entityData.equipment.pet.modifier;
                    pet->m_entityData.position = player->m_entityData.position;
                    pet->m_entityData.hp = pet->maxHP();
                    if (player->m_entityData.equipment.pet.spiritCount < 16) {
                        for (int i = 0; i < player->m_entityData.equipment.pet.spiritCount; i++)
                            pet->m_entityData.name[i] = player->m_entityData.equipment.pet.spirits[i].material;
                        pet->m_entityData.name[player->m_entityData.equipment.pet.spiritCount] = 0;
                    }
                    fillAppearance(pet->m_entityData.race, pet->m_entityData.appearance, 0);
                    initCompanion(player->m_id, pet);
                } else {
                    // Without a pet item the companion dies, unless a type-7 buff holds it.
                    Creature *companion = creatureByID(player->m_companionId);
                    bool held = false;
                    if (companion) {
                        for (std::list<Creature::Buff>::iterator buff = companion->m_buffs.begin();
                             buff != companion->m_buffs.end(); buff++) {
                            if (buff->type == BuffTameness) {
                                held = true;
                                break;
                            }
                        }
                    }
                    if (held)
                        continue;
                    player->m_companionId = 0;
                    if (companion)
                        companion->m_entityData.hp = 0.0f;
                }
            }
        }

        // The local player's pet item follows its companion's level and experience; a pet its owner no
        // longer keeps turns into an ordinary dead character.
        for (std::map<long long, Creature *>::iterator it = m_creatures.begin(); it != m_creatures.end(); it++) {
            Creature *&creature = it->second;
            if (!creature)
                continue;
            Creature *companion = creatureByID(creature->m_companionId);
            if (creature == m_localPlayer && companion &&
                m_localPlayer->m_entityData.equipment.pet.category == ItemCategoryPet) {
                creature->m_entityData.equipment.pet.level = (short)companion->m_entityData.level;
                creature->m_entityData.equipment.pet.modifier = companion->m_entityData.xp;
            }
            if (creature->m_entityData.hostilityType != HostilityPet)
                continue;
            Creature *owner = creatureByID(creature->m_entityData.ownerId);
            if (owner && owner->m_companionId != creature->m_id) {
                creature->reset();
                creature->m_entityData.hp = 0.0f;
                creature->m_entityData.hostilityType = HostilityFriendlyNpc;
                creature->m_entityData.timeSinceAbility = DismissedPetDeathAgeMs;
                creature->m_entityData.ownerId = 0;
            }
        }
        despawned.clear();

        // The dead: a bomb (race 144) explodes the moment it dies; dead pets come back to their owners
        // after 20 seconds; other dead creatures are removed after a second.
        for (std::map<long long, Creature *>::iterator it = m_creatures.begin(); it != m_creatures.end(); it++) {
            Creature *&entry = it->second;
            Creature *dead = entry;
            if (!dead || !(0.0f >= dead->m_entityData.hp) || dead->m_entityData.hostilityType == HostilityPlayer)
                continue;
            if (dead->m_entityData.timeSinceAbility == 0 && dead->m_entityData.race == RaceBomb) {
                plasma::Vector<3, Fixed> &center = dead->m_entityData.position;
                ServerUpdates::ParticlePacket smoke;
                smoke.position = center;
                smoke.count = BombSmokeParticleCount;
                smoke.size = 0.25f;
                // Particle RGBA palette for this effect; channels and alpha stay together.
                smoke.type = ParticleFire;
                smoke.color = plasma::Vector<4, float>(1.0f, 0.2f, 0.5f);
                smoke.acceleration = plasma::Vector<3, float>(0.0f, 0.0f, 20.0f);
                outgoing->particles.push_back(smoke);
                // Every destructible block within eight blocks is blown away in a burst of its colour.
                bool exploded = false;
                for (int x = -8; x <= 8; x++) {
                    for (int y = -8; y <= 8; y++) {
                        for (int z = -8; z <= 8; z++) {
                            plasma::Vector<3, Fixed> position =
                                center + plasma::Vector<3, Fixed>(plasma::Vector<3, int>(x, y, z));
                            if (!(plasma::Vector<3, float>(position - center).squaredLength() < 64.0f))
                                continue;
                            Fixed &blockZ = position[2];
                            Fixed &blockY = position[1];
                            Fixed &blockX = position[0];
                            const Block &block = getBlock(blockX, blockY, blockZ, 0);
                            if (!block.isDestructible())
                                continue;
                            ServerUpdates::ParticlePacket debris;
                            debris.position = position;
                            debris.size = 0.5f;
                            debris.color = plasma::Vector<4, float>(block.color[0] / 255.0f, block.color[1] / 255.0f,
                                                                    block.color[2] / 255.0f, 1.0f);
                            debris.acceleration = plasma::Vector<3, float>(0.0f, 0.0f, 10.0f);
                            debris.count = BlockDebrisParticleCount;
                            outgoing->particles.push_back(debris);
                            destroyBlock((int)blockX, (int)blockY, toBlock(blockZ));
                            ServerUpdates::BlockPacket change;
                            change.position = toBlock(position);
                            change.day = m_day;
                            change.block = airCube;
                            outgoing->blocks.push_back(change);
                            exploded = true;
                        }
                    }
                }
                if (exploded) {
                    ServerUpdates::SoundPacket sound;
                    sound.position = center;
                    sound.sound = SoundBlade1; // id 1
                    sound.pitch = (float)rand() * 0.4f / float(RAND_MAX) + 0.5f;
                    outgoing->sounds.push_back(sound);
                    if (!m_localPlayer) {
                        Fixed &y = center[1];
                        Fixed &x = center[0];
                        computeLight((int)(x - LightRecomputeMargin), (int)(y - LightRecomputeMargin),
                                     (int)(x + LightRecomputeMargin), (int)(y + LightRecomputeMargin),
                                     LightRecomputeMargin, 0);
                    }
                    enterZones();
                    Fixed &y = center[1];
                    Fixed &x = center[0];
                    removeUnsupportedPlants((int)(x - 8), (int)(y - 8), (int)(x + 8), (int)(y + 8));
                    leaveZones();
                }
                // The blast hits every living hostile creature within eight blocks of its edge, once.
                plasma::Vector<3, Fixed> origin = center;
                for (std::map<long long, Creature *>::iterator it2 = m_creatures.begin(); it2 != m_creatures.end();
                     it2++) {
                    Creature *victim = it2->second;
                    if (!victim || victim == dead || !isHostile(dead, victim) || victim->m_id == dead->m_petId ||
                        0.0f >= victim->m_entityData.hp || victim->m_entityData.rollTime != 0 ||
                        dead->m_struckCreatures.find(victim->m_id) != dead->m_struckCreatures.end())
                        continue;
                    plasma::Vector<3, float> &size = victim->m_entityData.appearance.size;
                    float reach = size[0] * 0.7f + 8.0f;
                    plasma::Vector<3, Fixed> &position = victim->m_entityData.position;
                    plasma::Vector<3, Fixed> offset = position - origin;
                    if (!(plasma::Vector<2, Fixed>(offset[0], offset[1]).squaredLength() < reach * reach))
                        continue;
                    float height = fabsf((float)(position[2] - origin[2]));
                    if (!(size[2] * 0.5f + dead->m_entityData.appearance.size[2] + 8.0f > height))
                        continue;
                    plasma::Vector<3, float> push(position - center);
                    float &up = push[2];
                    up = 0.0f;
                    if (push.squaredLength() > 0.01f)
                        push.normalize();
                    up = 0.25f;
                    bool critical = rand() % BombCriticalChanceDenominator == 0;
                    push *= 10.0f;
                    float damage = dead->attackPower() * 25.0f;
                    if (critical)
                        damage *= 2.0f;
                    dead->m_struckCreatures.insert(victim->m_id);
                    attackCreature(victim, dead, damage, critical, true, 0.0f, center, push, outgoing, &changedZones,
                                   true, 0, 0, true);
                }
            }
            dead->m_entityData.timeSinceAbility += milliseconds;
            if (dead->m_entityData.hostilityType == HostilityPet) {
                long long &ownerId = dead->m_entityData.ownerId;
                if (creatureByID(ownerId) && dead->m_entityData.timeSinceAbility >= 20000 &&
                    creatureByID(ownerId)->m_entityData.hp >= 0.0f) {
                    dead->m_entityData.hp = dead->maxHP();
                    dead->m_entityData.position = creatureByID(ownerId)->m_entityData.position;
                    dead->m_opponentId = 0;
                    dead->m_aggro.clear();
                    creatureByID(ownerId)->m_hitCounts.clear();
                }
            } else if (dead->m_entityData.timeSinceAbility >= 1000) {
                despawned.push_back(it);
                for (std::map<long long, Creature *>::iterator other = m_creatures.begin(); other != m_creatures.end();
                     other++) {
                    Creature *&creature = other->second;
                    if (creature) {
                        creature->m_aggro.erase(entry->m_id);
                        creature->m_hitCounts.erase(entry->m_id);
                    }
                }
            }
        }
        for (std::list<std::map<long long, Creature *>::iterator>::iterator it = despawned.begin();
             it != despawned.end(); it++) {
            std::map<long long, Creature *>::iterator &entry = *it;
            if (entry->second)
                delete entry->second;
            m_creatures.erase(entry);
        }

        // The m_airships records further than 512 blocks from every player are dropped.
        std::list<std::map<long long, Zone::Airship *>::iterator> farRecords;
        for (std::map<long long, Zone::Airship *>::iterator it = m_airships.begin(); it != m_airships.end(); it++) {
            Zone::Airship *&record = it->second;
            if (!record)
                continue;
            bool nearby = false;
            for (std::list<Creature *>::iterator player = players.begin(); player != players.end(); player++) {
                if (plasma::Vector<3, float>(record->position - (*player)->m_entityData.position).squaredLength() <
                    262144.0f) {
                    nearby = true;
                    break;
                }
            }
            if (!nearby)
                farRecords.push_back(it);
        }
        for (std::list<std::map<long long, Zone::Airship *>::iterator>::iterator it = farRecords.begin();
             it != farRecords.end(); it++) {
            std::map<long long, Zone::Airship *>::iterator &entry = *it;
            if (entry->second)
                delete entry->second;
            m_airships.erase(entry);
        }
    }

    // A named world's server side brings the zones around the players to life: their moving objects
    // get a world entry, and their spawns whose hours have come and that a player is near create their
    // creatures.
    if (!m_client && !m_name.empty()) {
        for (std::set<Zone *>::iterator it = playerZones.begin(); it != playerZones.end(); it++) {
            Zone *const &zone = *it;
            for (int i = 0; i < (int)zone->m_airships.size(); i++) {
                Zone::Airship *record = zone->m_airships[i];
                if (m_airships.find(record->id) != m_airships.end())
                    continue;
                for (std::list<Creature *>::iterator player = players.begin(); player != players.end(); player++) {
                    if (plasma::Vector<3, float>(record->position - (*player)->m_entityData.position).squaredLength() <
                        262144.0f) {
                        Zone::Airship *entry = new Zone::Airship(*record);
                        m_airships[entry->id] = entry;
                        entry->currentPosition = entry->position;
                        entry->currentRotation = entry->rotation;
                        break;
                    }
                }
            }
            Region::Record *square = getRecord(zone->m_position[0] / 8, zone->m_position[1] / 8);
            for (int index = 0; index < (int)zone->m_spawns.size(); index++) {
                Spawn *spawn = zone->m_spawns[index];
                if (spawn->activeFrom != spawn->activeUntil &&
                    (getTime() < spawn->activeFrom || getTime() > spawn->activeUntil))
                    continue;
                if (creatureByID(spawn->creatureId))
                    continue;
                // A killed spawn waits for its timer, or for the next day.
                if (spawn->respawnTimeRemaining < 0)
                    spawn->respawnTimeRemaining = 0;
                if (spawn->respawnTimeRemaining != 0 && spawn->deathDay == m_day)
                    continue;
                bool nearby = false;
                for (std::list<Creature *>::iterator player = players.begin(); player != players.end(); player++) {
                    float radius = spawn->spawnRadius;
                    if (radius * radius >
                        plasma::Vector<3, float>((*player)->m_entityData.position - spawn->position).squaredLength()) {
                        nearby = true;
                        break;
                    }
                }
                if (!nearby)
                    continue;
                // The residents of a square with a mission of kind 5 take the mission's level and equipment
                // rarity.
                if (square && square->mission.kind == MissionDungeon &&
                    (spawn->appearance.appearanceFlags & (1 << AppearanceFlagMissionLevel))) {
                    if (spawn->hostilityType != HostilityAttackableObject) {
                        spawn->level = square->mission.level;
                        spawn->rarity = square->mission.rarity;
                    }
                    srand(square->mission.seed + (int)spawn->creatureId);
                    equip(spawn, true);
                    if (spawn->appearance.appearanceFlags & (1 << AppearanceFlagTinted)) {
                        if (spawn->missionResident)
                            spawn->appearance.appearanceFlags |= (1 << AppearanceFlagMissionResident);
                    } else {
                        spawn->equipment.weaponRight.varyLevel(0.05f, false);
                        spawn->equipment.weaponLeft.varyLevel(0.05f, false);
                        spawn->equipment.shoulder.varyLevel(0.05f, false);
                        spawn->equipment.chest.varyLevel(0.05f, false);
                        spawn->equipment.hands.varyLevel(0.05f, false);
                        spawn->equipment.feet.varyLevel(0.05f, false);
                        spawn->equipment.neck.varyLevel(0.05f, false);
                        spawn->equipment.ringLeft.varyLevel(0.05f, false);
                        spawn->equipment.ringRight.varyLevel(0.05f, false);
                    }
                    std::vector<std::vector<ItemStack>> &tabs = spawn->inventory.m_inventoryTabs;
                    for (std::vector<std::vector<ItemStack>>::iterator tab = tabs.begin(); tab != tabs.end(); tab++) {
                        std::vector<ItemStack> &stacks = *tab;
                        for (std::vector<ItemStack>::iterator stack = stacks.begin(); stack != stacks.end(); stack++) {
                            ItemStack &entry = *stack;
                            if (entry.quantity == 0 || !entry.item.scalesWithLevel())
                                continue;
                            entry.item.level = (short)square->mission.level;
                            unsigned char category = entry.item.category;
                            if (category == ItemCategoryWeapon || category == ItemCategoryShoulder ||
                                category == ItemCategoryHands || category == ItemCategoryChest ||
                                category == ItemCategoryFeet || category == ItemCategoryNeck ||
                                category == ItemCategoryRing) {
                                entry.item.rarity = square->mission.rarity + 1;
                                if (rand() % DungeonLootFirstRarityChanceDenominator == 0)
                                    entry.item.rarity++;
                                if (rand() % DungeonLootSecondRarityChanceDenominator == 0)
                                    entry.item.rarity++;
                                if (entry.item.rarity > ItemRarityYellow)
                                    entry.item.rarity = ItemRarityYellow;
                            }
                        }
                    }
                }
                Creature *creature = new Creature(spawn->creatureId);
                m_creatures[spawn->creatureId] = creature;
                creature->m_entityData.position = spawn->position;
                creature->m_entityData.rotation[2] = spawn->rotation;
                creature->m_entityData.level = spawn->level;
                creature->m_entityData.hostilityType = (unsigned char)spawn->hostilityType;
                creature->m_entityData.race = spawn->race;
                creature->m_entityData.classType = spawn->classType;
                creature->m_entityData.specialization = spawn->specialization;
                creature->m_entityData.rarity = spawn->rarity;
                creature->m_entityData.appearance = spawn->appearance;
                creature->m_entityData.equipment = spawn->equipment;
                creature->m_entityData.multipliers = spawn->multipliers;
                creature->m_entityData.origin = spawn->position;
                creature->m_entityData.setToggles((1 << CreatureToggle3), spawn->unknown050 != 0);
                creature->m_entityData.spawnSlot =
                    plasma::Vector<3, int>(zone->m_position[0], zone->m_position[1], index);
                creature->m_entityData.speechExpression = spawn->speechExpression;
                creature->m_entityData.questZone = spawn->questZone;
                creature->m_entityData.unused19c = spawn->unused10c8;
                strcpy(creature->m_entityData.name, spawn->name);
                std::vector<Creature::ScheduleEntry> &schedule = spawn->schedule;
                creature->m_schedule = schedule;
                creature->m_despawnDistance = spawn->spawnRadius;
                creature->generationKey = spawn->generationKey;
                creature->m_dungeonMissionTarget = spawn->missionResident;
                for (int entry = 0; entry < (int)schedule.size(); entry++) {
                    if (getTime() >= schedule[entry].time)
                        creature->m_scheduleReached = entry;
                }
                creature->m_scheduleEntry = creature->m_scheduleReached;
                // Its behaviour: the spawn's own, or fighting, looking at players and, unless it is an object, a
                // trader or flag 0x40 holds it, walking about.
                if (spawn->m_behavior) {
                    creature->m_behavior = spawn->m_behavior->clone();
                } else if (spawn->hostilityType != HostilityAttackableObject) {
                    SequentialBehavior *behavior = new SequentialBehavior();
                    Behavior *combat = new CombatBehavior(20.0f);
                    behavior->m_behaviors.push_back(combat);
                    Behavior *look = new LookAtPlayerBehavior();
                    behavior->m_behaviors.push_back(look);
                    unsigned char classType = creature->m_entityData.classType;
                    if (creature->m_entityData.hostilityType != HostilityAttackableObject &&
                        classType != ClassInnkeeper && classType != ClassAnalyzer && classType != ClassItemVendor &&
                        classType != ClassArmorVendor && classType != ClassWeaponVendor && classType != ClassSmith &&
                        classType != ClassCarpenter && classType != ClassTailor &&
                        !(creature->m_entityData.appearance.appearanceFlags & (1 << AppearanceFlagClassMaster))) {
                        Behavior *walk = new RandomWalkBehavior();
                        behavior->m_behaviors.push_back(walk);
                    }
                    creature->m_behavior = behavior;
                }
                creature->m_specialSkills = spawn->specialSkills;
                creature->m_summonAppearanceMode = spawn->summonAppearanceMode;
                creature->m_summonRaces = spawn->summonRaces;
                creature->m_inventory = spawn->inventory;
                creature->m_entityData.hp = creature->maxHP();
                if (spawn->appearance.appearanceFlags & (1 << AppearanceFlagTinted))
                    creature->m_entityData.appearance.size *= 2.0f;
                creature->m_entityData.position[2] += Fixed(creature->m_entityData.appearance.size[2] * 0.5f + 0.1f);
            }
        }
    }

    // Every creature's step.
    for (std::map<long long, Creature *>::iterator it = m_creatures.begin(); it != m_creatures.end(); it++) {
        Creature *creature = it->second;
        if (!creature)
            continue;
        // The interactions the creature asked for since the last step.
        for (std::list<Interaction>::iterator it2 = creature->m_interactions.begin();
             it2 != creature->m_interactions.end(); it2++) {
            Interaction &interaction = *it2;
            switch (interaction.type) {
            case InteractionTalk:
                // An innkeeper lets the player sleep from six in the evening to seven in the morning of the next
                // day.
                if (m_client)
                    break;
                for (std::map<long long, Creature *>::iterator other = m_creatures.begin(); other != m_creatures.end();
                     other++) {
                    Creature *npc = other->second;
                    if (!npc || !(npc->m_entityData.spawnSlot == interaction.position))
                        continue;
                    if (npc->m_entityData.classType == ClassInnkeeper &&
                        (m_time >= 18 * MillisecondsPerHour || m_time <= 6 * MillisecondsPerHour)) {
                        m_day++;
                        m_time = 7 * MillisecondsPerHour;
                        ServerUpdates::SoundPacket sound;
                        sound.sound = SoundLevelUp2;
                        sound.position = creature->m_entityData.position;
                        sound.volume = 0.75f;
                        outgoing->sounds.push_back(sound);
                        resetCreatures();
                        clearRegionPopulationState(outgoing);
                        break;
                    }
                    if (npc->m_entityData.appearance.appearanceFlags & (1 << AppearanceFlagHasDialog))
                        break;
                }
                break;
            case InteractionCallCompanion:
                // The companion forgets its enemies and comes back when it is more than 200 blocks away.
                if (m_client)
                    break;
                {
                    Creature *companion = creatureByID(creature->m_companionId);
                    if (companion) {
                        companion->m_aggro.clear();
                        plasma::Vector<3, Fixed> &position = creature->m_entityData.position;
                        if (plasma::Vector<3, float>(companion->m_entityData.position - position).squaredLength() >
                            40000.0f)
                            companion->m_entityData.position = position;
                    }
                    creature->m_hitCounts.clear();
                    creature->m_opponentId = 0;
                }
                break;
            case InteractionPickUp: {
                // A dropped item of the zone within four blocks.
                if (m_client)
                    break;
                plasma::Vector<3, int> &id = interaction.position;
                Zone *zone = this->zone(id[0], id[1]);
                if (!zone)
                    break;
                int &index = id[2];
                if (index < 0 || index >= (int)zone->m_items.size())
                    break;
                Zone::ZoneItem &dropped = zone->m_items[index];
                if (!creature->canUseFormula(dropped.item) ||
                    !(16.0f >=
                      plasma::Vector<3, float>(dropped.position - creature->m_entityData.position).squaredLength()))
                    break;
                changedZones.insert(zone);
                ServerUpdates::ItemPickupPacket pickup;
                pickup.creatureId = creature->m_id;
                pickup.item = dropped.item;
                outgoing->pickedUpItems.push_back(pickup);
                if (creature == m_localPlayer && dropped.item.category != ItemCategoryManaCube)
                    creature->m_inventory.add(dropped.item, -1);
                ServerUpdates::SoundPacket sound;
                sound.sound = SoundPickup;
                sound.pitch = (float)rand() * 0.1f / float(RAND_MAX) + 1.0f;
                sound.position = creature->m_entityData.position;
                outgoing->sounds.push_back(sound);
                zone->m_items.erase(zone->m_items.begin() + index);
                break;
            }
            case InteractionUseObject: {
                // Using an object ends stealth (ability 79).
                if (creature->m_entityData.currentAbility == AbilitySneak) {
                    creature->m_entityData.currentAbility = AbilityNone;
                    creature->m_stealth = 0.0f;
                }
                plasma::Vector<3, int> &id = interaction.position;
                Zone *zone = this->zone(id[0], id[1]);
                if (!zone || id[2] < 0)
                    break;
                std::vector<Zone::Object> &objects = zone->m_objects;
                if (id[2] >= (int)objects.size())
                    break;
                Zone::Object &object = objects[id[2]];
                if (object.type == StaticObjectRuneStone) {
                    // A rune stone takes the local player to the stone it is linked with (linkedObjectIndex) in the
                    // nearest object's zone.
                    if (creature != m_localPlayer)
                        break;
                    plasma::Vector<3, Fixed> &position = creature->m_entityData.position;
                    plasma::Vector<3, int> nearest = nearestObject(position);
                    Zone *target = this->zone(nearest[0], nearest[1]);
                    if (!target || object.linkedObjectIndex < 0 ||
                        object.linkedObjectIndex >= (int)target->m_objects.size())
                        break;
                    ServerUpdates::SoundPacket sound;
                    sound.sound = SoundUpgrade2;
                    sound.pitch = 1.0f;
                    sound.position = position;
                    outgoing->sounds.push_back(sound);
                    position = target->m_objects[object.linkedObjectIndex].position +
                               plasma::Vector<3, Fixed>(Fixed(2), Fixed(0), Fixed(0));
                    break;
                }
                if ((object.type == StaticObjectStool || object.type == StaticObjectBench ||
                     object.type == StaticObjectBed || object.type == StaticObjectSleepingMat ||
                     object.type == StaticObjectBeachTowel) &&
                    object.occupantId == 0 &&
                    (creature->m_entityData.currentAbility == AbilityNone ||
                     creature->m_entityData.currentAbility == AbilitySit ||
                     creature->m_entityData.currentAbility == AbilitySleep)) {
                    // Sitting down or lying down on it.
                    if (!m_client) {
                        object.occupantId = creature->m_id;
                        switch (object.type) {
                        case StaticObjectStool:
                        case StaticObjectBench:
                            creature->m_entityData.position =
                                object.position +
                                plasma::Vector<3, Fixed>(plasma::Vector<3, float>(
                                    0.0f, 0.0f, creature->m_entityData.appearance.size[2] * 0.5f + object.size[2]));
                            creature->m_entityData.velocity = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
                            creature->m_entityData.rotation[2] = (float)((object.rotation + 2) * 90);
                            creature->m_entityData.currentAbility = AbilitySit;
                            break;
                        case StaticObjectBed:
                        case StaticObjectBeachTowel:
                        case StaticObjectSleepingMat:
                            creature->m_entityData.position =
                                object.position +
                                plasma::Vector<3, Fixed>(plasma::Vector<3, float>(
                                    0.0f, 0.0f, creature->m_entityData.appearance.size[2] * 0.5f + object.size[2]));
                            creature->m_entityData.velocity = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
                            creature->m_entityData.rotation[2] = (float)(object.rotation * 90);
                            creature->m_entityData.currentAbility = AbilitySleep;
                            break;
                        }
                        outgoing->objects.push_back(ServerUpdates::ZoneObjectPacket(id, object));
                    }
                    creature->m_entityData.acceleration = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
                    break;
                }
                if (m_client)
                    break;
                // A stocked chest (state 2) spills its inventory on first use.
                if (object.type == StaticObjectChest && object.active == StaticObjectStockedChest) {
                    std::vector<std::vector<ItemStack>> &tabs = object.inventory.m_inventoryTabs;
                    for (int tab = 0; tab < (int)tabs.size(); tab++) {
                        for (std::vector<ItemStack>::iterator stack = tabs[tab].begin(); stack != tabs[tab].end();
                             stack++) {
                            ItemStack &entry = *stack;
                            while (entry.quantity != 0) {
                                // The original draws y, x (the offset is built first), then the rotation.
                                plasma::Vector<3, Fixed> offset(
                                    plasma::Vector<3, float>(2.0f - (float)rand() * 4.0f / float(RAND_MAX),
                                                             2.0f - (float)rand() * 4.0f / float(RAND_MAX), 0.0f));
                                dropItem(entry.item, object.position + offset, (float)rand() * 360.0f / float(RAND_MAX),
                                         1.0f, &changedZones);
                                entry.removeOne();
                            }
                        }
                    }
                }
                // Doors, windows, levers and chests toggle.
                if (object.type == StaticObjectLever || object.type == StaticObjectDoor ||
                    object.type == StaticObjectBigDoor || object.type == StaticObjectWindow ||
                    object.type == StaticObjectChest) {
                    setObjectActive(&object, outgoing, object.active == StaticObjectInactive);
                    outgoing->objects.push_back(ServerUpdates::ZoneObjectPacket(id, object));
                }
                // A lever sets its linked object (linkedObjectIndex) to its own state; the packet sent is the
                // lever's.
                if (object.type == StaticObjectLever) {
                    int linked = object.linkedObjectIndex;
                    if (linked < 0 || linked >= (int)objects.size())
                        break;
                    setObjectActive(&objects[linked], outgoing, object.active != StaticObjectInactive);
                    outgoing->objects.push_back(ServerUpdates::ZoneObjectPacket(id, object));
                }
                break;
            }
            case InteractionUseConsumable:
                if (m_client)
                    break;
                if (interaction.item.category == ItemCategoryConsumable && interaction.item.id == ConsumableBomb) {
                    // A bomb: a creature of race 144 at the player's place, with the item's level.
                    long long id = 0;
                    for (std::map<long long, Creature *>::iterator other = m_creatures.begin();
                         other != m_creatures.end(); other++) {
                        if (other->second && other->first < id)
                            id = other->first;
                    }
                    id--;
                    Creature *bomb = new Creature(id);
                    m_creatures[id] = bomb;
                    bomb->m_entityData.position = creature->m_entityData.position;
                    bomb->m_entityData.level = interaction.item.level;
                    int &race = bomb->m_entityData.race;
                    race = RaceBomb;
                    bomb->m_entityData.hostilityType = HostilityAttackableObject;
                    fillAppearance(race, bomb->m_entityData.appearance, 0);
                } else {
                    // Food and potions heal by their effect value.
                    float healing = interaction.item.effectValue();
                    creature->m_entityData.hp += healing;
                    ServerUpdates::SoundPacket sound;
                    plasma::Vector<3, Fixed> &position = creature->m_entityData.position;
                    sound.position = position;
                    sound.pitch = 1.0f;
                    sound.sound = SoundDrink2;
                    outgoing->sounds.push_back(sound);
                    HitPacket heal;
                    heal.position = position;
                    heal.targetId = creature->m_id;
                    heal.attackerId = 0;
                    heal.damage = -healing;
                    heal.critical = 0;
                    outgoing->hits.push_back(heal);
                }
                break;
            case InteractionBuild: {
                // A building model (buildingModel) placed in front of the creature, turned by rotation quarter
                // turns: its voxels become blocks, empty voxels between its lowest and highest voxel inside air.
                if (m_client)
                    break;
                Sprite *model = buildingSprite(creature->m_entityData.buildingModel);
                if (!model)
                    break;
                plasma::Vector<3, int> base = buildingPosition(plasma::Vector<3, int>(
                    creature->m_entityData.position + plasma::Vector<3, Fixed>(creature->m_entityData.attackRotation) +
                    plasma::Vector<3, Fixed>(plasma::Vector<3, float>(0.5f, 0.5f, 0.5f))));
                plasma::Matrix<float> placement;
                placement.identity();
                placement.rotateZ((float)(signed char)creature->m_entityData.interactionState * 90.0f);
                placement.translate((float)model->sizeX() * -0.5f, (float)model->sizeY() * -0.5f, 0.0f);
                enterZones();
                for (int x = 0; x < model->sizeX(); x++) {
                    for (int y = 0; y < model->sizeY(); y++) {
                        int lowest = model->sizeZ();
                        int highest = 0;
                        for (int z = 0; z < model->sizeZ(); z++) {
                            if (model->voxel(x, y, z) != Sprite::s_emptyVoxel) {
                                if (z < lowest)
                                    lowest = z;
                                if (z > highest)
                                    highest = z;
                            }
                        }
                        for (int z = 0; z < model->sizeZ(); z++) {
                            plasma::Vector<3, unsigned char> color;
                            color = model->voxel(x, y, z);
                            plasma::Vector<3, int> block(
                                plasma::Vector<3, Fixed>(base) +
                                plasma::Vector<3, Fixed>(placement *
                                                         (plasma::Vector<3, float>((float)x, (float)y, (float)z) +
                                                          plasma::Vector<3, float>(0.5f, 0.5f, 0.5f))));
                            if (color == Sprite::s_emptyVoxel) {
                                if (z > lowest && z < highest)
                                    setBlock(block[0], block[1], block[2], Block(color, (1 << BlockFlagInterior)), 0);
                            } else if (!(getBlock(block[0], block[1], block[2], 0).type & (1 << BlockFlagInterior))) {
                                setBlock(block[0], block[1], block[2],
                                         Block(color, (1 << BlockFlagInterior) | BlockBuilding), 0);
                                ServerUpdates::BlockPacket change;
                                change.block = Block(color, (1 << BlockFlagInterior) | BlockBuilding);
                                change.day = m_day;
                                change.position = plasma::Vector<3, int>(block[0], block[1], block[2]);
                                outgoing->blocks.push_back(change);
                            }
                        }
                    }
                }
                if (!m_localPlayer) {
                    float baseY = (float)base[1];
                    float baseX = (float)base[0];
                    computeLight(
                        (int)(baseX - (float)model->sizeX() * 0.5f), (int)(baseY - (float)model->sizeY() * 0.5f),
                        (int)((float)model->sizeY() * 0.5f + baseX), (int)((float)model->sizeZ() * 0.5f + baseY), 8, 0);
                }
                int &baseX = base[0];
                int &baseY = base[1];
                removeUnsupportedPlants(baseX, baseY, baseX + model->sizeX(), baseY + model->sizeY());
                Zone *zone = this->zone(baseX / BlocksPerZone, baseY / BlocksPerZone);
                if (zone) {
                    Zone::Building building;
                    building.position = base;
                    building.model = creature->m_entityData.buildingModel;
                    zone->m_buildings.push_back(building);
                    buildingZones.insert(zone);
                }
                leaveZones();
                break;
            }
            case InteractionDrop:
                // Dropping the interaction's item at the creature's feet.
                if (m_client)
                    break;
                dropItem(interaction.item, creature->m_entityData.position, 0.0f, 1.0f, &changedZones);
                {
                    ServerUpdates::SoundPacket sound;
                    sound.sound = SoundDrop;
                    sound.position = creature->m_entityData.position;
                    outgoing->sounds.push_back(sound);
                }
                break;
            }
        }
        if (!m_client)
            creature->m_interactions.clear();
        if (creature->m_entityData.hp > creature->maxHP())
            creature->m_entityData.hp = creature->maxHP();
        if (0.0f >= creature->m_entityData.hp)
            continue;
        // A computer-controlled creature thinks every 32 ms (not while stunned).
        if (creature->m_entityData.hostilityType != HostilityPlayer && !m_client) {
            creature->m_behaviorTime += milliseconds;
            if (creature->m_entityData.stunTime <= 0 && creature->m_behaviorTime >= 32) {
                if (creature->m_behavior)
                    creature->m_behavior->Update(creature, this, creature->m_behaviorTime, outgoing);
                creature->m_behaviorTime = 0;
            }
            if (creature->m_entityData.stunTime > 0)
                creature->m_entityData.setToggles((1 << CreatureToggleDodge), false);
        }
        // A bomb's fuse burns down its hp (2.5 % of a second's worth).
        if (creature->m_entityData.race == RaceBomb && !m_client) {
            creature->m_entityData.hp -= elapsed * 0.025f;
            if (0.0f > creature->m_entityData.hp)
                creature->m_entityData.hp = 0.0f;
        }
        // Following the path found for it, aiming at its last block.
        if (!m_client) {
            Creature::EntityData &entity = creature->m_entityData;
            entity.setToggles((1 << CreatureToggleDodge), false);
            if (!creature->m_path.empty() && entity.stunTime <= 0) {
                entity.setToggles((1 << CreatureToggleFaceAim), false);
                if (followPath(creature, milliseconds)) {
                    finishPathSearch(creature);
                } else {
                    if (!creature->m_path.empty()) {
                        entity.setToggles((1 << CreatureToggleFaceAim), true);
                        entity.attackRotation = plasma::Vector<3, float>(
                            plasma::Vector<3, Fixed>(creature->m_path.back()) +
                            plasma::Vector<3, Fixed>(Fixed(0.5), Fixed(0.5), Fixed(0.5)) - entity.position);
                    }
                    entity.setToggles((1 << CreatureToggleDodge), true);
                }
            }
        }
        // The ability timers.
        creature->m_entityData.actionLockTime -= milliseconds;
        if (creature->m_entityData.actionLockTime > 0)
            creature->m_entityData.timeSinceAbility = creature->attackDuration() + 1;
        if (creature->m_entityData.actionLockTime < 0)
            creature->m_entityData.actionLockTime = 0;
        creature->m_entityData.slowedTime -= milliseconds;
        if (creature->m_entityData.slowedTime < 0)
            creature->m_entityData.slowedTime = 0;
        creature->m_entityData.sprintTime -= milliseconds;
        if (creature->m_entityData.sprintTime < 0)
            creature->m_entityData.sprintTime = 0;
        float &recovery = creature->m_entityData.revivalPenalty;
        if (0.0f > recovery) {
            recovery += seconds / 180.0f;
            if (recovery > 0.0f)
                recovery = 0.0f;
        }

        // The ability in progress.
        switch (creature->m_entityData.currentAbility) {
        case AbilityTeleportToCity:
            // The local player is carried to the town square (a record of kind 1) of its region when the
            // anticipation ends.
            if (creature == m_localPlayer && creature->m_entityData.timeSinceAbility <= creature->anticipationTime() &&
                creature->m_entityData.timeSinceAbility + milliseconds > creature->anticipationTime()) {
                Region *region = regionAtColumn((int)m_localPlayer->m_entityData.position[0],
                                                (int)m_localPlayer->m_entityData.position[1]);
                if (region) {
                    Region::Record *record = region->m_records;
                    for (int x = 8; x != 0; x--) {
                        for (int y = 8; y != 0; y--, record++) {
                            if (record->kind != Region::Record::SceneryCity)
                                continue;
                            plasma::Vector<2, Fixed> &center = record->center;
                            plasma::Vector<3, Fixed> &position = creature->m_entityData.position;
                            position[0] = center[0];
                            position[1] = center[1];
                            position[2] = 0;
                        }
                    }
                }
            }
            break;
        case Ability47:
        case AbilityIntercept:
            // A sound as the ability starts.
            if (creature->m_entityData.timeSinceAbility <= 0 &&
                creature->m_entityData.timeSinceAbility + milliseconds > 0) {
                ServerUpdates::SoundPacket sound;
                sound.position = creature->m_entityData.position;
                sound.sound = SoundSmashJump;
                sound.pitch = (float)rand() * 0.25f / float(RAND_MAX) + 1.0f;
                outgoing->sounds.push_back(sound);
            }
            break;
        case AbilityBlock:
            // Charging: a sound when the anticipation ends, then one every 200 ms, higher with the charge.
            if ((m_client || creature->m_entityData.hostilityType == HostilityPlayer) && creature != m_localPlayer)
                break;
            if (creature->m_entityData.timeSinceAbility <= creature->anticipationTime() &&
                creature->m_entityData.timeSinceAbility + milliseconds > creature->anticipationTime()) {
                ServerUpdates::SoundPacket sound;
                sound.position = creature->m_entityData.position;
                sound.sound = SoundShieldSwing;
                sound.pitch = (float)rand() * 0.25f / float(RAND_MAX) + 1.0f;
                outgoing->sounds.push_back(sound);
            }
            if ((creature->m_entityData.timeSinceAbility + milliseconds) / 200 !=
                creature->m_entityData.timeSinceAbility / 200) {
                ServerUpdates::SoundPacket sound;
                sound.position = creature->m_entityData.position;
                sound.sound = SoundCharge2;
                sound.pitch = creature->m_entityData.charge * 0.5f + 1.0f;
                outgoing->sounds.push_back(sound);
            }
            break;
        case AbilityRightSlash:
        case AbilityLeftSlash:
        case Ability3:
        case Ability4:
        case AbilityPerforate:
        case AbilityRightStab:
        case AbilityLeftStab:
        case AbilityShieldAttack:
        case AbilityBlockJab:
        case AbilitySwirlStrike:
        case AbilityCrossedWeaponSweep:
        case AbilityLeftSweep:
        case AbilityRightSweep:
        case AbilityOverheadChop:
        case AbilityMonsterChargeStrike:
        case AbilityAmbush:
        case AbilityLeftDaggerPierce:
        case AbilityRightDaggerPierce:
        case AbilityKick:
        case AbilityRangerKick:
        case AbilityFireSwirl:
        case AbilityFireVortex:
        case AbilityWaterSwirl:
        case AbilityWaterVortex:
        case AbilityForwardRoll:
        case AbilitySmash:
        case AbilityTwoHandedSweep:
        case AbilityTwoHandedThrust:
        case AbilityCrushStrike:
        case AbilityGuardianCrushStrike:
        case Ability62:
        case AbilityTwoHandedSpin:
        case AbilityTwoHandedSlam:
        case AbilityTwoHandedSwing:
        case AbilityHeadbutt:
        case AbilityHornCharge:
        case AbilityTailStrike:
        case AbilityShellSpin:
        case AbilityStomp:
        case AbilityBite:
        case AbilityDashForward:
        case AbilityDashLeft:
        case AbilityDashRight:
        case AbilityCyclone:
        case AbilityExplosion:
        case AbilityFireExplosion:
        case AbilityQuake:
        case Ability93:
        case AbilityBlockStrike: {
            // The melee attacks and close spells. A remote player's attack is its own client's business.
            if (!m_client && creature->m_entityData.hostilityType == HostilityPlayer && creature != m_localPlayer)
                break;
            int &time = creature->m_entityData.timeSinceAbility;
            // A ninja rolls into some attacks.
            int ability = creature->m_entityData.currentAbility;
            if (creature->m_entityData.classType == ClassRogue &&
                creature->m_entityData.specialization == SpecializationNinja &&
                (ability == AbilityAmbush || ability == AbilityPerforate || ability == AbilityKick) && time == 0)
                creature->m_entityData.rollTime = RollDurationMs;
            int anticipation = creature->anticipationTime();
            ability = creature->m_entityData.currentAbility;
            if (ability == AbilityExplosion || ability == AbilityFireExplosion) {
                // A shockwave: a rumble while gathering, a burst when it goes off.
                if (time < creature->anticipationTime() &&
                    (time + milliseconds) / SpellChannelSoundPeriodMs != time / SpellChannelSoundPeriodMs) {
                    ServerUpdates::SoundPacket sound;
                    sound.position = creature->m_entityData.position;
                    sound.sound = SoundChannel2;
                    sound.pitch = (float)rand() * 0.25f / float(RAND_MAX) + 1.0f;
                    outgoing->sounds.push_back(sound);
                }
                if (time - milliseconds < creature->anticipationTime() && time >= creature->anticipationTime()) {
                    ServerUpdates::ParticlePacket wave;
                    wave.position = creature->m_entityData.position;
                    wave.size = 0.25f;
                    wave.type = ParticleFire;
                    wave.spread = 20.0f;
                    wave.acceleration = plasma::Vector<3, float>(0.0f, 0.0f, 2.0f);
                    wave.count = ShockwaveParticleCount;
                    ServerUpdates::SoundPacket sound;
                    sound.position = creature->m_entityData.position;
                    sound.pitch = 1.0f;
                    sound.sound = SoundExplosion;
                    outgoing->sounds.push_back(sound);
                    outgoing->particles.push_back(wave);
                }
            } else if (ability == AbilityFireSwirl || ability == AbilityFireVortex || ability == AbilityWaterSwirl ||
                       ability == AbilityWaterVortex) {
                // A close spell: a rumble while gathering, then a burst of fire or (water mages) water at the aim.
                if (time < creature->anticipationTime() && (time + milliseconds) / 200 != time / 200) {
                    ServerUpdates::SoundPacket sound;
                    sound.position = creature->m_entityData.position;
                    sound.sound = SoundChannel2;
                    sound.pitch = (float)rand() * 0.25f / float(RAND_MAX) + 1.0f;
                    outgoing->sounds.push_back(sound);
                }
                if (time < creature->anticipationTime() && time + milliseconds >= creature->anticipationTime()) {
                    ServerUpdates::ParticlePacket burst;
                    burst.position = creature->m_entityData.position +
                                     plasma::Vector<3, Fixed>(creature->m_entityData.attackRotation);
                    burst.size = 0.25f;
                    // Particle RGBA palette for this effect; channels and alpha stay together.
                    if (creature->m_entityData.specialization == SpecializationWater) {
                        burst.color = plasma::Vector<4, float>(0.0f, 0.2f, 1.0f, 1.0f);
                        burst.type = ParticleGlow;
                    } else {
                        burst.type = ParticleFire;
                    }
                    burst.spread = 8.0f;
                    burst.acceleration = plasma::Vector<3, float>(0.0f, 0.0f, 2.0f);
                    burst.count = CloseSpellParticleCount;
                    ServerUpdates::SoundPacket sound;
                    sound.position = creature->m_entityData.position;
                    sound.sound =
                        creature->m_entityData.specialization == SpecializationWater ? SoundWatersplash : SoundFireball;
                    sound.pitch = 1.0f;
                    outgoing->sounds.push_back(sound);
                    outgoing->particles.push_back(burst);
                }
            } else if (time == 0 && creature->hasTwoHandedWeapon() &&
                       creature->m_entityData.currentAbility != AbilityQuake) {
                // Lifting a two-handed weapon.
                ServerUpdates::SoundPacket sound;
                sound.position = creature->m_entityData.position;
                sound.pitch = (float)rand() * 0.1f / float(RAND_MAX) + 1.0f;
                sound.sound = SoundSwingSlow;
                outgoing->sounds.push_back(sound);
            }
            // The swing sound as the strike begins.
            if (time <= anticipation && time + milliseconds > anticipation &&
                creature->m_entityData.currentAbility != AbilityQuake) {
                ServerUpdates::SoundPacket sound;
                sound.position = creature->m_entityData.position;
                sound.pitch = (float)rand() * 0.2f / float(RAND_MAX) + 0.9f;
                int swingAbility = creature->m_entityData.currentAbility;
                if (swingAbility == AbilityBlockJab) {
                    sound.sound = SoundShieldSwing;
                } else if (swingAbility == AbilitySwirlStrike) {
                    sound.sound = SoundSwirl;
                } else if (swingAbility == AbilitySmash) {
                    sound.sound = SoundSmashJump;
                } else {
                    float pitch = sound.pitch * 1.5f;
                    sound.pitch = pitch;
                    int heavySwingAbility = creature->m_entityData.currentAbility;
                    if (creature->hasTwoHandedWeapon() || heavySwingAbility == AbilityHeadbutt ||
                        heavySwingAbility == Ability93 || heavySwingAbility == AbilityHornCharge)
                        sound.pitch = pitch * 0.5f;
                    sound.sound = SoundSwing;
                }
                outgoing->sounds.push_back(sound);
            }
            ability = creature->m_entityData.currentAbility;
            if ((ability == AbilityTwoHandedSweep || ability == AbilityCrushStrike ||
                 ability == AbilityTwoHandedThrust || ability == AbilityStomp) &&
                time < creature->anticipationTime() + creature->strikeTime() / 2 &&
                time + milliseconds >= creature->anticipationTime() + creature->strikeTime() / 2) {
                ServerUpdates::SoundPacket sound;
                sound.position = creature->m_entityData.position;
                sound.sound = SoundSlamGround;
                sound.pitch = (float)rand() * 0.25f / float(RAND_MAX) + 1.0f;
                outgoing->sounds.push_back(sound);
            }
            // The hit window: the 100 ms before the middle of the strike, within it; a few attacks hit in
            // periods all through it.
            int windowStart = creature->strikeTime() / 2 - 100 + creature->anticipationTime();
            int windowEnd = creature->anticipationTime() + creature->strikeTime() / 2;
            if (windowStart < creature->anticipationTime())
                windowStart = creature->anticipationTime();
            if (windowEnd >= creature->anticipationTime() + creature->strikeTime())
                windowEnd = creature->anticipationTime() - 1 + creature->strikeTime();
            ability = creature->m_entityData.currentAbility;
            if (ability == AbilityHeadbutt || ability == AbilityHornCharge)
                windowStart = creature->strikeTime() / HeadbuttHitStartDivisor + creature->anticipationTime();
            if (creature->m_entityData.currentAbility == Ability93) {
                windowStart = creature->strikeTime() / Ability93HitStartDivisor + creature->anticipationTime();
                windowEnd = creature->anticipationTime() + creature->strikeTime();
            }
            bool newPeriod = false;
            ability = creature->m_entityData.currentAbility;
            if (ability == AbilityShellSpin || ability == AbilityCyclone || ability == AbilityFireSwirl ||
                ability == AbilityFireVortex || ability == AbilityWaterSwirl || ability == AbilityWaterVortex ||
                ability == AbilitySwirlStrike || ability == AbilityPerforate || ability == AbilityQuake) {
                if (time >= creature->anticipationTime() &&
                    time < creature->anticipationTime() + creature->strikeTime()) {
                    int period = creature->m_entityData.currentAbility == AbilityQuake ? QuakePulseMs
                                                                                       : DefaultRepeatingStrikePulseMs;
                    if (creature->m_entityData.currentAbility == AbilitySwirlStrike)
                        period = creature->strikeTime() / SwirlStrikeAndPerforateHitsPerStrike;
                    if (creature->m_entityData.currentAbility == AbilityPerforate)
                        period = creature->strikeTime() / 3;
                    ability = creature->m_entityData.currentAbility;
                    if (ability == AbilityFireSwirl || ability == AbilityWaterSwirl)
                        period = creature->strikeTime() / CloseSwirlHitsPerStrike;
                    ability = creature->m_entityData.currentAbility;
                    if (ability == AbilityFireVortex || ability == AbilityWaterVortex)
                        period = creature->strikeTime() / VortexHitsPerStrike;
                    windowStart =
                        (time - creature->anticipationTime()) / period * period + creature->anticipationTime();
                    windowEnd = windowStart + 100;
                    int next = (time + milliseconds - creature->anticipationTime()) / period * period +
                               creature->anticipationTime();
                    if (next != windowStart && next < creature->anticipationTime() + creature->strikeTime()) {
                        creature->m_swingCount = creature->m_entityData.hitCombo;
                        newPeriod = true;
                        creature->m_struckCreatures.clear();
                        creature->m_dodgingCreatures.clear();
                    }
                    if (time + milliseconds > windowEnd)
                        newPeriod = true;
                    if (creature->m_entityData.currentAbility != AbilityPerforate &&
                        (time - milliseconds < creature->anticipationTime() ||
                         (time + milliseconds - creature->anticipationTime()) / period !=
                             (time - creature->anticipationTime()) / period)) {
                        ServerUpdates::SoundPacket sound;
                        sound.position = creature->m_entityData.position;
                        sound.sound = SoundSwing;
                        sound.pitch = (float)rand() * 0.25f / float(RAND_MAX) + 1.0f;
                        outgoing->sounds.push_back(sound);
                        if (creature->m_entityData.currentAbility == AbilityQuake) {
                            ServerUpdates::SoundPacket roar;
                            roar.position = creature->m_entityData.position;
                            roar.sound = SoundPunch282;
                            roar.pitch = (float)rand() * 0.1f / float(RAND_MAX) + 0.5f;
                            outgoing->sounds.push_back(roar);
                        }
                    }
                } else {
                    windowStart = creature->anticipationTime();
                    windowEnd = windowStart;
                }
            }
            // Until the strike: the dodges of the last attack are settled and the struck set starts empty.
            if (time <= creature->anticipationTime()) {
                creature->m_swingCount = creature->m_entityData.hitCombo;
                for (std::set<long long>::iterator dodger = creature->m_dodgingCreatures.begin();
                     dodger != creature->m_dodgingCreatures.end(); dodger++) {
                    if (creature->m_struckCreatures.find(*dodger) != creature->m_struckCreatures.end())
                        continue;
                    Creature *target = creatureByID(*dodger);
                    if (!target)
                        continue;
                    target->applyDodgeBonus(outgoing);
                    HitPacket hit;
                    hit.damage = 0.0f;
                    hit.attackerId = creature->m_id;
                    hit.targetId = target->m_id;
                    hit.hitType = HitTypeDodge;
                    hit.critical = 0;
                    hit.position = target->m_entityData.position;
                    outgoing->hits.push_back(hit);
                }
                creature->m_struckCreatures.clear();
                creature->m_dodgingCreatures.clear();
                creature->m_strikeLanded = 0;
            }
            if (creature->m_entityData.currentAbility == AbilityStomp &&
                time < creature->strikeTime() / 2 + creature->anticipationTime() &&
                time + milliseconds >= creature->strikeTime() / 2 + creature->anticipationTime()) {
                ServerUpdates::SoundPacket roar;
                roar.position = creature->m_entityData.position;
                roar.sound = SoundPunch282;
                roar.pitch = (float)rand() * 0.1f / float(RAND_MAX) + 0.5f;
                outgoing->sounds.push_back(roar);
            }
            // After the window the dodges count.
            if (time >= windowEnd) {
                for (std::set<long long>::iterator dodger = creature->m_dodgingCreatures.begin();
                     dodger != creature->m_dodgingCreatures.end(); dodger++) {
                    if (creature->m_struckCreatures.find(*dodger) != creature->m_struckCreatures.end())
                        continue;
                    Creature *target = creatureByID(*dodger);
                    if (!target)
                        continue;
                    target->applyDodgeBonus(outgoing);
                    HitPacket hit;
                    hit.damage = 0.0f;
                    hit.attackerId = creature->m_id;
                    hit.targetId = target->m_id;
                    hit.hitType = HitTypeDodge;
                    hit.critical = 0;
                    hit.position = target->m_entityData.position;
                    outgoing->hits.push_back(hit);
                }
                creature->m_dodgingCreatures.clear();
            }
            // A strike that hit nothing ends the combo and the attack.
            if (!creature->m_strikeLanded &&
                time + milliseconds >= creature->anticipationTime() + creature->strikeTime() &&
                time < creature->attackDuration() && creature->m_entityData.rollTime == 0) {
                float tempo = creature->tempo();
                creature->m_entityData.hitCombo = 0;
                time = (int)(tempo / creature->tempo() * (float)time);
            }
            // The attack's power (m_attackPower): its mana cost, a spender's whole mana, a dive's or
            // shockwave's 1, or the charge.
            ability = creature->m_entityData.currentAbility;
            float &mana = creature->m_entityData.mp;
            float &power = creature->m_attackPower;
            if (ability == AbilityFireVortex || ability == AbilityWaterVortex) {
                if (time <= creature->anticipationTime() && time + milliseconds > creature->anticipationTime()) {
                    power = 0.1f;
                    mana -= creature->manaCost(creature->m_entityData.currentAbility, -1);
                    if (0.0f > mana)
                        mana = 0.0f;
                }
            } else if (time == 0) {
                creature->m_criticalStrike = 0;
                power = 0.0f;
                if (creature->manaCost(creature->m_entityData.currentAbility, -1) > 0.0f) {
                    power = creature->manaCost(creature->m_entityData.currentAbility, -1);
                    mana -= power;
                } else {
                    int manaSpendingAbility = creature->m_entityData.currentAbility;
                    if (manaSpendingAbility == AbilityAmbush || manaSpendingAbility == AbilityKick ||
                        manaSpendingAbility == AbilityPerforate) {
                        power = mana;
                        mana = 0.0f;
                        // Buff 11 makes it a critical hit, once.
                        if (creature->findBuff(BuffGuaranteedCritical)) {
                            Creature::Buff buff;
                            buff.type = BuffGuaranteedCritical;
                            buff.timeRemaining = 0;
                            creature->setBuff(buff);
                            BuffPacket packet;
                            packet.sourceId = creature->m_id;
                            packet.targetId = creature->m_id;
                            packet.buff = buff;
                            outgoing->buffs.push_back(packet);
                            creature->m_criticalStrike = 1;
                        }
                    } else if (manaSpendingAbility == AbilitySmash || manaSpendingAbility == AbilityRangerKick ||
                               manaSpendingAbility == AbilityExplosion || manaSpendingAbility == AbilityFireExplosion) {
                        power = 1.0f;
                    }
                }
            }
            float &charge = creature->m_entityData.charge;
            if (charge > 0.0f) {
                power = charge;
                mana -= charge;
                creature->m_strikeLanded = 0;
                int chargedAbility = creature->m_entityData.currentAbility;
                if (chargedAbility == AbilitySwirlStrike || chargedAbility == AbilityPerforate)
                    power *= 0.5f;
            }
            charge = 0.0f;
            if (0.0f > mana)
                mana = 0.0f;
            bool powered = power > 0.0f || creature->m_entityData.currentAbility == AbilitySwirlStrike;
            if (!(time > windowStart && time <= windowEnd))
                break;

            // The strike: where it lands and how far it reaches.
            plasma::Matrix<float> turning;
            turning.identity();
            turning.rotateZ(creature->m_entityData.rotation[2]);
            float forwardShare = 1.5f;
            float &width = creature->m_entityData.appearance.size[0];
            float reach = width * 1.5f;
            int strikeAbility = creature->m_entityData.currentAbility;
            if (strikeAbility == AbilityLeftSweep || strikeAbility == AbilityRightSweep ||
                strikeAbility == AbilityLeftSlash || strikeAbility == AbilityRightSlash ||
                strikeAbility == AbilityShieldAttack || strikeAbility == Ability4 || strikeAbility == Ability3 ||
                strikeAbility == AbilityLeftStab || strikeAbility == AbilityRightStab ||
                strikeAbility == AbilityLeftDaggerPierce || strikeAbility == AbilityRightDaggerPierce) {
                forwardShare = 1.0f;
                reach = width * 2.0f;
            }
            if (strikeAbility == AbilityKick || strikeAbility == AbilityRangerKick)
                reach *= 2.0f;
            if (strikeAbility == AbilityHeadbutt || strikeAbility == AbilityTailStrike)
                forwardShare = 0.0f;
            if (strikeAbility == AbilityCyclone || strikeAbility == Ability93 || strikeAbility == AbilityBlockStrike) {
                reach *= 2.0f;
                forwardShare = 0.0f;
            }
            if (strikeAbility == AbilityExplosion || strikeAbility == AbilityFireExplosion) {
                reach = 8.0f;
                forwardShare = 0.0f;
            }
            if (strikeAbility == AbilityFireSwirl || strikeAbility == AbilityWaterSwirl) {
                reach = 3.0f;
                forwardShare = 0.0f;
            }
            if (strikeAbility == AbilityFireVortex || strikeAbility == AbilityWaterVortex) {
                reach = 5.0f;
                forwardShare = 0.0f;
            }
            if (strikeAbility == AbilityQuake || strikeAbility == AbilityStomp) {
                reach = 20.0f;
                forwardShare = 0.0f;
            }
            if (strikeAbility == AbilityShellSpin || strikeAbility == AbilityDashRight ||
                strikeAbility == AbilityDashLeft || strikeAbility == AbilityDashForward) {
                reach *= 1.2f;
                forwardShare = 0.0f;
            }
            plasma::Vector<3, Fixed> &position = creature->m_entityData.position;
            plasma::Vector<3, Fixed> center =
                position + plasma::Vector<3, Fixed>(
                               turning.transformDirection(plasma::Vector<3, float>(0.0f, width * forwardShare, 0.0f)));
            strikeAbility = creature->m_entityData.currentAbility;
            if (strikeAbility == AbilityFireSwirl || strikeAbility == AbilityFireVortex ||
                strikeAbility == AbilityWaterSwirl || strikeAbility == AbilityWaterVortex)
                center = position + plasma::Vector<3, Fixed>(creature->m_entityData.attackRotation);
            strikeAbility = creature->m_entityData.currentAbility;
            if (strikeAbility == AbilityBite || strikeAbility == AbilityHornCharge)
                center[2] -= Fixed((creature->m_entityData.appearance.size[2] - width) * 0.5f);
            strikeAbility = creature->m_entityData.currentAbility;
            if (strikeAbility == AbilitySwirlStrike || strikeAbility == AbilityGuardianCrushStrike ||
                strikeAbility == AbilitySmash) {
                center = position;
                reach *= 3.0f;
            }
            // A big creature's strike smashes the destructible blocks in its reach.
            strikeAbility = creature->m_entityData.currentAbility;
            if (!m_client && width > 4.0f && strikeAbility != AbilityExplosion && strikeAbility != AbilityQuake &&
                strikeAbility != AbilityStomp) {
                bool smashed = false;
                int radius = (int)reach;
                if (radius > MeleeTerrainBreakRadiusCap)
                    radius = MeleeTerrainBreakRadiusCap;
                for (int x = -radius; x <= radius; x++) {
                    for (int y = -radius; y <= radius; y++) {
                        for (int z = -radius; z <= radius; z++) {
                            plasma::Vector<3, Fixed> spot =
                                center + plasma::Vector<3, Fixed>(plasma::Vector<3, int>(x, y, z));
                            if (!((float)(radius * radius) > plasma::Vector<3, float>(spot - center).squaredLength()))
                                continue;
                            Fixed &spotZ = spot[2];
                            Fixed &spotY = spot[1];
                            Fixed &spotX = spot[0];
                            const Block &block = getBlock(spotX, spotY, spotZ, 0);
                            if (!block.isDestructible())
                                continue;
                            ServerUpdates::ParticlePacket debris;
                            debris.position = spot;
                            debris.size = 0.5f;
                            debris.color = plasma::Vector<4, float>(block.color[0] / 255.0f, block.color[1] / 255.0f,
                                                                    block.color[2] / 255.0f, 1.0f);
                            debris.acceleration = plasma::Vector<3, float>(0.0f, 0.0f, 10.0f);
                            debris.count = 3;
                            outgoing->particles.push_back(debris);
                            destroyBlock((int)spotX, (int)spotY, (int)spotZ);
                            ServerUpdates::BlockPacket change;
                            change.position = plasma::Vector<3, int>(spot);
                            change.day = m_day;
                            change.block = airCube;
                            outgoing->blocks.push_back(change);
                            smashed = true;
                        }
                    }
                }
                if (smashed) {
                    ServerUpdates::SoundPacket sound;
                    sound.position = creature->m_entityData.position;
                    sound.sound = SoundBlade2;
                    sound.pitch = (float)rand() * 0.4f / float(RAND_MAX) + 0.5f;
                    outgoing->sounds.push_back(sound);
                    Fixed &centerY = center[1];
                    Fixed &centerX = center[0];
                    if (!m_localPlayer)
                        computeLight((int)(centerX - reach), (int)(centerY - reach), (int)(centerX + reach),
                                     (int)(centerY + reach), 8, 0);
                    removeUnsupportedPlants((int)(centerX - reach), (int)(centerY - reach), (int)(centerX + reach),
                                            (int)(centerY + reach));
                }
            }
            // Bashes and spins hurt by their power only.
            float bonus = 1.0f;
            strikeAbility = creature->m_entityData.currentAbility;
            if (strikeAbility == AbilityCrushStrike || strikeAbility == AbilitySwirlStrike ||
                strikeAbility == Ability62)
                bonus = 0.0f;
            bool hitAny = false;
            // A fire spell (31) uses up buff 9.
            if (creature->m_entityData.currentAbility == AbilityFireVortex && time >= creature->anticipationTime() &&
                creature->findBuff(BuffInstantFire)) {
                Creature::Buff buff;
                buff.type = BuffInstantFire;
                buff.timeRemaining = 0;
                creature->setBuff(buff);
                BuffPacket packet;
                packet.sourceId = creature->m_id;
                packet.targetId = creature->m_id;
                packet.buff = buff;
                outgoing->buffs.push_back(packet);
            }
            for (std::map<long long, Creature *>::iterator other = m_creatures.begin(); other != m_creatures.end();
                 other++) {
                Creature *target = other->second;
                if (!target || 0.0f >= target->m_entityData.hp)
                    continue;
                long long &targetId = target->m_id;
                if (creature->m_struckCreatures.find(targetId) != creature->m_struckCreatures.end())
                    continue;
                plasma::Vector<3, float> &targetSize = target->m_entityData.appearance.size;
                float limit = targetSize[0] * 0.7f + reach;
                plasma::Vector<3, Fixed> origin = creature->m_entityData.position;
                strikeAbility = creature->m_entityData.currentAbility;
                if (strikeAbility == AbilityFireSwirl || strikeAbility == AbilityFireVortex ||
                    strikeAbility == AbilityWaterSwirl || strikeAbility == AbilityWaterVortex)
                    origin = center + plasma::Vector<3, Fixed>(Fixed(0), Fixed(0), Fixed(reach));
                plasma::Vector<3, Fixed> &targetPosition = target->m_entityData.position;
                plasma::Vector<3, Fixed> offset = center - targetPosition;
                if (!(plasma::Vector<2, Fixed>(offset[0], offset[1]).squaredLength() < limit * limit))
                    continue;
                float height = fabsf((float)(center[2] - targetPosition[2]));
                if (!(targetSize[2] * 0.5f + creature->m_entityData.appearance.size[2] + reach > height))
                    continue;
                if (!inRange(origin, targetPosition, true, 200.0f))
                    continue;
                if (isHostile(creature, target)) {
                    if (target->m_entityData.rollTime != 0) {
                        creature->m_dodgingCreatures.insert(targetId);
                        continue;
                    }
                    plasma::Vector<3, float> push(targetPosition - creature->m_entityData.position);
                    float &up = push[2];
                    up = 0.0f;
                    if (push.squaredLength() > 0.01f)
                        push.normalize();
                    up = 0.25f;
                    bool critical = creature->m_criticalStrike || creature->rollCritical();
                    float knockback = creature->knockback(creature->m_entityData.currentAbility);
                    push *= sqrtf(width / targetSize[0]);
                    push *= knockback;
                    float damage;
                    float variance = 0.0f;
                    strikeAbility = creature->m_entityData.currentAbility;
                    if (strikeAbility != AbilityExplosion && strikeAbility != AbilityFireExplosion &&
                        strikeAbility != AbilityFireSwirl && strikeAbility != AbilityFireVortex &&
                        strikeAbility != AbilityWaterSwirl && strikeAbility != AbilityWaterVortex) {
                        float strength = power;
                        damage = creature->damageScale(creature->m_entityData.currentAbility) *
                                 creature->attackPower() * (strength * strength * 5.0f + bonus);
                        damage = (1.25f - (float)rand() * 0.5f / float(RAND_MAX) + variance) * damage;
                    } else {
                        damage = creature->damageScale(creature->m_entityData.currentAbility) * creature->spellPower();
                        damage = (1.25f - (float)rand() * 0.5f / float(RAND_MAX)) * damage * (variance + 1.0f);
                    }
                    if (critical)
                        damage *= 2.0f;
                    bool firstHit = creature->m_struckCreatures.empty() && creature->m_entityData.stunTime <= 0;
                    creature->m_struckCreatures.insert(targetId);
                    creature->m_strikeLanded = 1;
                    strikeAbility = creature->m_entityData.currentAbility;
                    bool magical = strikeAbility == AbilityFireSwirl || strikeAbility == AbilityFireVortex ||
                                   strikeAbility == AbilityWaterSwirl || strikeAbility == AbilityWaterVortex;
                    if (attackCreature(target, creature, damage, critical, powered, power,
                                       creature->m_entityData.position, push, outgoing, &changedZones, magical,
                                       creature->m_entityData.currentAbility, 0, true)) {
                        // A fire mage's fire spell (30) may give buff 9.
                        if (creature->m_entityData.currentAbility == AbilityFireSwirl &&
                            creature->hasRandomChance(0.25f) &&
                            creature->m_entityData.specialization == SpecializationFire) {
                            Creature::Buff buff;
                            buff.type = BuffInstantFire;
                            buff.timeRemaining = InstantFireBuffDurationMs;
                            creature->setBuff(buff);
                            BuffPacket packet;
                            packet.sourceId = creature->m_id;
                            packet.targetId = creature->m_id;
                            packet.buff = buff;
                            outgoing->buffs.push_back(packet);
                            ServerUpdates::SoundPacket sound;
                            sound.sound = SoundUpgrade2;
                            sound.position = creature->m_entityData.position;
                            outgoing->sounds.push_back(sound);
                        }
                        if (firstHit) {
                            creature->m_entityData.hitCombo++;
                            creature->onHit();
                            creature->m_entityData.timeSinceHit = 0;
                        }
                        // An assassin's powered hit builds stealth.
                        if (powered && creature->m_entityData.classType == ClassRogue &&
                            creature->m_entityData.specialization == SpecializationAssassin) {
                            creature->m_stealth += power;
                            if (creature->m_stealth > 1.0f)
                                creature->m_stealth = 1.0f;
                        }
                        // The first unpowered hit gives mana.
                        strikeAbility = creature->m_entityData.currentAbility;
                        if ((!m_client || creature == m_localPlayer) && !hitAny && !powered &&
                            strikeAbility != AbilityFireSwirl && strikeAbility != AbilityWaterSwirl) {
                            float gain = (1.0f - (float)rand() * 2.0f / float(RAND_MAX)) * 0.05f + 0.1f;
                            gain = (float)creature->attackDuration() / 500.0f * gain;
                            if (rand() % ManaBonusChanceDenominator == 0)
                                gain *= 1.5f;
                            mana += gain;
                            if (mana > 1.0f)
                                mana = 1.0f;
                        }
                        hitAny = true;
                    }
                }
                // A water mage's water spell (32, 33) heals its allies in each new period.
                strikeAbility = creature->m_entityData.currentAbility;
                if ((strikeAbility == AbilityWaterVortex || strikeAbility == AbilityWaterSwirl) && newPeriod &&
                    !isHostile(creature, target) && creature->m_entityData.specialization == SpecializationWater) {
                    HitPacket heal;
                    heal.position = targetPosition;
                    float variance2 = 0.0f;
                    float roll = 1.25f - (float)rand() * 0.5f / float(RAND_MAX);
                    heal.damage = creature->spellPower() * roll * (variance2 + 1.0f) * -0.1f;
                    heal.critical = creature->rollCritical();
                    heal.attackerId = creature->m_id;
                    heal.targetId = targetId;
                    outgoing->hits.push_back(heal);
                    ServerUpdates::SoundPacket sound;
                    sound.position = creature->m_entityData.position;
                    sound.pitch = 1.0f;
                    sound.sound = SoundWatersplashHit;
                    outgoing->sounds.push_back(sound);
                    if (!m_client || (creature == m_localPlayer && target == m_localPlayer))
                        applyHit(heal, outgoing, &changedZones);
                }
            }
            if (!hitAny)
                break;
            // (the code reads the attack duration here and ignores it.)
            if (creature->m_entityData.equipment.weaponLeft.id != WeaponShield &&
                (creature->m_entityData.equipment.weaponRight.category == ItemCategoryNone ||
                 creature->m_entityData.equipment.weaponLeft.category == ItemCategoryWeapon))
                creature->attackDuration();
            break;
        }
        case AbilityHealingStream: {
            // A healing beam: a rumble while gathering, then eight heals of the focused creature (or itself)
            // over the strike, each costing mana.
            int &time = creature->m_entityData.timeSinceAbility;
            if (time < creature->anticipationTime() && (time + milliseconds) / 200 != time / 200) {
                ServerUpdates::SoundPacket sound;
                sound.position = creature->m_entityData.position;
                sound.sound = SoundChannel2;
                sound.pitch = (float)rand() * 0.25f / float(RAND_MAX) + 1.0f;
                outgoing->sounds.push_back(sound);
            }
            if ((m_client || creature->m_entityData.hostilityType == HostilityPlayer) && creature != m_localPlayer)
                break;
            if (time <= creature->anticipationTime() || time > creature->anticipationTime() + creature->strikeTime())
                break;
            int period = (int)((float)creature->strikeTime() * 0.125f);
            if ((time + milliseconds - creature->anticipationTime()) / period ==
                (time - creature->anticipationTime()) / period)
                break;
            if (creature->m_entityData.mp < creature->manaCost(creature->m_entityData.currentAbility, -1))
                break;
            creature->m_entityData.mp -= creature->manaCost(creature->m_entityData.currentAbility, -1);
            Creature *target = creatureByID(creature->m_entityData.focusId);
            if (!target)
                target = creature;
            HitPacket heal;
            heal.position = target->m_entityData.position;
            float roll = 1.25f - (float)rand() * 0.5f / float(RAND_MAX);
            heal.damage = creature->spellPower() * -2.0f * roll;
            heal.critical = creature->rollCritical();
            heal.attackerId = creature->m_id;
            heal.targetId = target->m_id;
            outgoing->hits.push_back(heal);
            ServerUpdates::SoundPacket sound;
            sound.position = creature->m_entityData.position;
            sound.pitch = 1.0f;
            sound.sound = SoundWatersplash;
            outgoing->sounds.push_back(sound);
            if (!m_client || (creature == m_localPlayer && target == m_localPlayer))
                applyHit(heal, outgoing, &changedZones);
            break;
        }
        case AbilityBulwark: {
            // Bulwark: ten seconds of buff 1, which ends a stun.
            if ((m_client || creature->m_entityData.hostilityType == HostilityPlayer) && creature != m_localPlayer)
                break;
            if (creature->m_entityData.timeSinceAbility != 0)
                break;
            ServerUpdates::SoundPacket sound;
            sound.position = creature->m_entityData.position;
            sound.pitch = 1.0f;
            sound.sound = SoundShieldSlam;
            outgoing->sounds.push_back(sound);
            Creature::Buff buff;
            buff.timeRemaining = 10000;
            buff.type = BuffBulwark;
            creature->setBuff(buff);
            if (creature->m_entityData.stunTime > 0)
                creature->m_entityData.stunTime = 0;
            BuffPacket packet;
            packet.sourceId = creature->m_id;
            packet.targetId = creature->m_id;
            packet.buff = buff;
            outgoing->buffs.push_back(packet);
            break;
        }
        case AbilityManaShield: {
            // Mana shield: thirty seconds of buff 6, absorbing four times the spell damage.
            if ((m_client || creature->m_entityData.hostilityType == HostilityPlayer) && creature != m_localPlayer)
                break;
            if (creature->m_entityData.timeSinceAbility != 0)
                break;
            ServerUpdates::SoundPacket sound;
            sound.position = creature->m_entityData.position;
            sound.pitch = 1.0f;
            sound.sound = SoundShieldSlam;
            outgoing->sounds.push_back(sound);
            Creature::Buff buff;
            buff.timeRemaining = 30000;
            buff.strength = creature->spellPower() * 4.0f;
            buff.type = BuffManashield;
            creature->setBuff(buff);
            if (creature->m_entityData.stunTime > 0)
                creature->m_entityData.stunTime = 0;
            BuffPacket packet;
            packet.sourceId = creature->m_id;
            packet.targetId = creature->m_id;
            packet.buff = buff;
            outgoing->buffs.push_back(packet);
            break;
        }
        case AbilityLava:
        case AbilityWaterCloud: {
            // A lingering spell: a cloud projectile of fire (89) or water (90) where the caster stands, eight
            // blocks wide.
            int &time = creature->m_entityData.timeSinceAbility;
            if (time < creature->anticipationTime() && (time + milliseconds) / 200 != time / 200) {
                ServerUpdates::SoundPacket sound;
                sound.position = creature->m_entityData.position;
                sound.sound = SoundChannel2;
                sound.pitch = (float)rand() * 0.25f / float(RAND_MAX) + 1.0f;
                outgoing->sounds.push_back(sound);
            }
            if ((m_client || creature->m_entityData.hostilityType == HostilityPlayer) && creature != m_localPlayer)
                break;
            if (time > creature->anticipationTime() || time + milliseconds <= creature->anticipationTime())
                break;
            plasma::Vector<3, Fixed> &position = creature->m_entityData.position;
            ServerUpdates::SoundPacket sound;
            sound.position = position;
            sound.sound = SoundFireball;
            sound.pitch = (float)rand() * 0.25f / float(RAND_MAX) + 1.0f;
            outgoing->sounds.push_back(sound);
            BulletPacket cloud;
            cloud.projectileType = ProjectileCloud;
            cloud.powered = 0;
            cloud.knockback = 0.0f;
            cloud.strength = 0.0f;
            cloud.damage = creature->damageScale(creature->m_entityData.currentAbility) * creature->spellPower() * 2.0f;
            cloud.ownerId = creature->m_id;
            cloud.element = creature->m_entityData.currentAbility == AbilityWaterCloud ? ElementWater : ElementFire;
            cloud.position = position;
            cloud.sizeInBlocks = 8.0f;
            cloud.visualSize = 1.0f;
            cloud.currentTime = 0;
            cloud.velocity = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
            m_projectiles.push_back(cloud);
            outgoing->projectiles.push_back(cloud);
            break;
        }
        case AbilityTeleport: {
            // A blink: to the free place nearest the aim within 20 blocks, then down to the ground (not into
            // water).
            if ((m_client || creature->m_entityData.hostilityType == HostilityPlayer) && creature != m_localPlayer)
                break;
            int &time = creature->m_entityData.timeSinceAbility;
            if (time > creature->anticipationTime() || time + milliseconds <= creature->anticipationTime())
                break;
            plasma::Vector<3, Fixed> &position = creature->m_entityData.position;
            plasma::Vector<3, Fixed> aim = position + plasma::Vector<3, Fixed>(creature->m_entityData.attackRotation);
            float nearest = 400.0f;
            plasma::Vector<3, Fixed> best = aim;
            plasma::Vector<3, float> &size = creature->m_entityData.appearance.size;
            for (int x = -TeleportSearchHorizontalRadius; x <= TeleportSearchHorizontalRadius; x++) {
                for (int y = -TeleportSearchHorizontalRadius; y <= TeleportSearchHorizontalRadius; y++) {
                    for (int z = -2; z <= TeleportSearchMaximumHeight; z++) {
                        plasma::Vector<3, Fixed> spot = aim + plasma::Vector<3, Fixed>(Fixed(x), Fixed(y), Fixed(z));
                        if (isBlocked(spot, size, false))
                            continue;
                        float distance = plasma::Vector<3, float>(spot - aim).squaredLength();
                        if (nearest > distance) {
                            nearest = distance;
                            best = spot;
                        }
                    }
                }
            }
            if (!(400.0f > nearest))
                break;
            position = best;
            while (!isBlocked(position - plasma::Vector<3, Fixed>(Fixed(0), Fixed(0), Fixed(1)), size, false)) {
                Fixed &height = position[2];
                if (getBlock(position[0], position[1], height - size[2] * 0.5f - 1, 0).blockType() == BlockWater)
                    break;
                height -= Fixed(1);
            }
            creature->m_fallStartHeight = (float)position[2];
            break;
        }
        case AbilitySummon: {
            // A summon (at most three at a time): a creature of one of the summoner's summon races, looking
            // like a random monster (mode 0), like the summoner (1) or like its target (2), that fights for
            // it.
            if ((m_client || creature->m_entityData.hostilityType == HostilityPlayer) && creature != m_localPlayer)
                break;
            int &time = creature->m_entityData.timeSinceAbility;
            if (time > creature->anticipationTime() || time + milliseconds <= creature->anticipationTime())
                break;
            int summons = 0;
            long long id = 0;
            for (std::map<long long, Creature *>::iterator other = m_creatures.begin(); other != m_creatures.end();
                 other++) {
                if (!other->second)
                    continue;
                if (other->second->m_entityData.ownerId == creature->m_id)
                    summons++;
                if (other->first < id)
                    id = other->first;
            }
            if (summons > 2)
                break;
            id--;
            Creature *summon = new Creature(id);
            summon->m_entityData.hostilityType =
                creature->m_entityData.hostilityType == HostilityPlayer ? HostilityFriendlyNpc : HostilityEnemy;
            std::vector<int> &races = creature->m_summonRaces;
            int race = !races.empty() ? races[rand() % races.size()] : RaceGreenSlime;
            switch (creature->m_summonAppearanceMode) {
            case SummonCopySummoner:
                summon->m_entityData.race = creature->m_entityData.race;
                summon->m_entityData.equipment = creature->m_entityData.equipment;
                summon->m_entityData.appearance = creature->m_entityData.appearance;
                break;
            case SummonCopyOpponent: {
                Creature *model = creatureByID(creature->m_opponentId);
                if (!model)
                    model = creature;
                summon->m_entityData.race = model->m_entityData.race;
                summon->m_entityData.equipment = model->m_entityData.equipment;
                summon->m_entityData.appearance = model->m_entityData.appearance;
                break;
            }
            default:
                summon->m_entityData.race = race;
                fillAppearance(summon->m_entityData.race, summon->m_entityData.appearance, 0);
                break;
            }
            summon->m_entityData.appearance.appearanceFlags =
                (summon->m_entityData.appearance.appearanceFlags & ~(1 << AppearanceFlagTinted)) |
                (1 << AppearanceFlagSummoned);
            summon->m_entityData.level = creature->m_entityData.level;
            summon->m_entityData.hp = creature->maxHP();
            summon->m_entityData.position = creature->m_entityData.position;
            SequentialBehavior *behavior = new SequentialBehavior();
            Behavior *combat = new CombatBehavior(20.0f);
            behavior->m_behaviors.push_back(combat);
            CompanionBehavior *follow = new CompanionBehavior();
            follow->m_ownerId = creature->m_id;
            Behavior *companion = follow;
            behavior->m_behaviors.push_back(companion);
            Behavior *walk = new RandomWalkBehavior();
            behavior->m_behaviors.push_back(walk);
            summon->m_behavior = behavior;
            summon->m_entityData.ownerId = creature->m_id;
            m_creatures[id] = summon;
            break;
        }
        case AbilityChargedShot:
        case AbilitySalvo:
        case AbilityChargeBoomerang:
        case AbilityChargeFireball:
        case AbilityCrush:
        case AbilitySwirl:
        case AbilityMonsterCharge:
            // Charged abilities: a sound every 200 ms, higher with the charge.
            if ((m_client || creature->m_entityData.hostilityType == HostilityPlayer) && creature != m_localPlayer)
                break;
            if ((creature->m_entityData.timeSinceAbility + milliseconds) / 200 !=
                creature->m_entityData.timeSinceAbility / 200) {
                ServerUpdates::SoundPacket sound;
                sound.position = creature->m_entityData.position;
                sound.sound = SoundCharge2;
                sound.pitch = creature->m_entityData.charge * 0.5f + 1.0f;
                outgoing->sounds.push_back(sound);
            }
            break;
        case AbilitySupportRay:
        case AbilityFireBeam:
        case AbilityFireRay: {
            // The rays: a continuous ray (28, costing mana while held), a short burst (94) and a stunning ray
            // (95). The ray goes up to 200 blocks to the first obstacle; every creature within a block of its
            // path is hit once per pulse.
            if ((m_client || creature->m_entityData.hostilityType == HostilityPlayer) && creature != m_localPlayer)
                break;
            int &time = creature->m_entityData.timeSinceAbility;
            float &mana = creature->m_entityData.mp;
            int ability = creature->m_entityData.currentAbility;
            if (ability == AbilityBlockStrike) {
                // (the code tests ability 104 here too: it never gets this far with it.)
                if (time == 0) {
                    mana -= creature->m_entityData.charge;
                    if (0.0f > mana)
                        mana = 0.0f;
                    creature->m_attackPower = creature->m_entityData.charge;
                    creature->m_entityData.charge = 0.0f;
                }
            } else {
                if (ability == AbilitySupportRay) {
                    mana -= elapsed * 0.0002f;
                } else if (time <= creature->anticipationTime() && time + milliseconds > creature->anticipationTime()) {
                    mana -= creature->manaCost(creature->m_entityData.currentAbility, -1);
                    if (creature->findBuff(BuffInstantFire)) {
                        Creature::Buff buff;
                        buff.type = BuffInstantFire;
                        buff.timeRemaining = 0;
                        creature->setBuff(buff);
                        BuffPacket packet;
                        packet.sourceId = creature->m_id;
                        packet.targetId = creature->m_id;
                        packet.buff = buff;
                        outgoing->buffs.push_back(packet);
                    }
                }
                // Without mana the ray does nothing this tick.
                if (0.0f > mana) {
                    mana = 0.0f;
                    break;
                }
            }
            if (time + milliseconds < creature->anticipationTime() ||
                time >= creature->anticipationTime() + creature->strikeTime())
                break;
            if (time > creature->anticipationTime() || time + milliseconds <= creature->anticipationTime()) {
                // After the first pulse only the held rays pulse, every 200 ms.
                int heldRayAbility = creature->m_entityData.currentAbility;
                if (heldRayAbility != AbilityFireRay && heldRayAbility != AbilitySupportRay)
                    break;
                if ((time + milliseconds - creature->anticipationTime()) / 200 ==
                    (time - creature->anticipationTime()) / 200)
                    break;
                if (time + milliseconds <= creature->anticipationTime())
                    break;
            }
            plasma::Vector<3, float> &aim = creature->m_entityData.attackRotation;
            plasma::Vector<3, float> direction = aim;
            if (direction.squaredLength() > 0.0f)
                direction.normalize();
            plasma::Vector<3, Fixed> origin = creature->sightPosition();
            float length = rayLength(origin, aim.normalized(), 200.0f, false, true);
            if (creature->m_entityData.currentAbility == AbilityBlockStrike)
                length = 10.0f;
            std::set<long long> &struck = creature->m_struckCreatures;
            struck.clear();
            creature->m_dodgingCreatures.clear();
            bool hitAny = false;
            // A water mage's ray leaves a healing cloud where it ends.
            if (creature->m_entityData.classType == ClassMage &&
                creature->m_entityData.specialization == SpecializationWater && time <= creature->anticipationTime() &&
                time + milliseconds > creature->anticipationTime()) {
                BulletPacket cloud;
                cloud.projectileType = ProjectileCloud;
                cloud.powered = 0;
                cloud.knockback = 0.0f;
                cloud.strength = 0.0f;
                cloud.damage = creature->spellPower() * 0.1f;
                cloud.ownerId = creature->m_id;
                cloud.sizeInBlocks = 4.0f;
                cloud.element = ElementWater;
                cloud.position = origin + plasma::Vector<3, Fixed>(length * aim.normalized());
                cloud.visualSize = 1.0f;
                cloud.currentTime = 0;
                cloud.velocity = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
                m_projectiles.push_back(cloud);
                outgoing->projectiles.push_back(cloud);
            }
            if (length > 0.0f) {
                float travelled = 0.0f;
                for (int i = 0; length > (float)i; travelled = (float)++i) {
                    for (std::map<long long, Creature *>::iterator other = m_creatures.begin();
                         other != m_creatures.end(); other++) {
                        Creature *target = other->second;
                        if (!target || target == creature)
                            continue;
                        long long &targetId = target->m_id;
                        if (targetId == creature->m_petId || 0.0f >= target->m_entityData.hp ||
                            struck.find(targetId) != struck.end() || target->m_entityData.rollTime != 0)
                            continue;
                        float radius = target->m_entityData.appearance.size[0] * 0.5f + 1.0f;
                        if (creature->m_entityData.currentAbility == AbilityBlockStrike)
                            radius += travelled;
                        plasma::Vector<3, Fixed> offset = origin - target->m_entityData.position;
                        if (!(plasma::Vector<2, Fixed>(offset[0], offset[1]).squaredLength() < radius * radius))
                            continue;
                        float height = fabsf((float)(origin[2] - target->m_entityData.position[2]));
                        if (!(target->m_entityData.appearance.size[2] * 0.5f + 1.0f > height))
                            continue;
                        struck.insert(targetId);
                        bool critical = creature->rollCritical();
                        plasma::Vector<3, float> away(target->m_entityData.position - creature->m_entityData.position);
                        float &up = away[2];
                        up = 0.0f;
                        if (away.squaredLength() > 0.01f)
                            away.normalize();
                        up = 0.25f;
                        float roll = (float)rand() * 0.05f / float(RAND_MAX) + 1.0f;
                        float damage = creature->spellPower() * 1.5f * roll;
                        if (creature->m_entityData.currentAbility == AbilityBlockStrike) {
                            float strength = creature->m_attackPower;
                            float variance = 0.0f;
                            damage = creature->damageScale(AbilityBlockStrike) * creature->attackPower() *
                                     (strength * strength * 10.0f);
                            damage = (1.25f - (float)rand() * 0.5f / float(RAND_MAX) + variance) * damage;
                        }
                        if (critical)
                            damage *= 2.0f;
                        if (!isHostile(creature, target))
                            continue;
                        plasma::Vector<3, float> knock(0.0f, 0.0f, 0.0f);
                        float stun = 0.0f;
                        if (creature->m_entityData.currentAbility == AbilityFireRay)
                            stun = 0.1f;
                        if (creature->m_entityData.currentAbility == AbilityBlockStrike) {
                            stun = creature->m_attackPower;
                            knock = aim.normalized() * 10.0f;
                            knock[2] = 5.0f;
                        }
                        int kind = creature->m_entityData.currentAbility;
                        bool magical = kind == AbilityFireRay || kind == AbilityFireBeam || kind == AbilitySupportRay;
                        bool powered = kind == AbilityFireRay || kind == AbilityBlockStrike;
                        if (attackCreature(target, creature, damage, critical, powered, stun, origin, knock, outgoing,
                                           &changedZones, magical, 0, 0, true) &&
                            creature->m_entityData.specialization == Specialization2) {
                            // Specialization 2 drains the damage as hp.
                            HitPacket drain;
                            drain.position = creature->m_entityData.position;
                            drain.critical = critical;
                            drain.attackerId = creature->m_id;
                            drain.targetId = creature->m_id;
                            damage = -damage;
                            drain.damage = damage;
                            outgoing->hits.push_back(drain);
                            if (!m_client) {
                                creature->m_entityData.hp -= damage;
                                if (creature->m_entityData.hp > creature->maxHP())
                                    creature->m_entityData.hp = creature->maxHP();
                            }
                        }
                        // A water mage's ray heals where it hits.
                        if (creature->m_entityData.classType == ClassMage &&
                            creature->m_entityData.specialization == SpecializationWater &&
                            time <= creature->anticipationTime() &&
                            time + milliseconds > creature->anticipationTime()) {
                            BulletPacket cloud;
                            cloud.projectileType = ProjectileCloud;
                            cloud.powered = 0;
                            cloud.knockback = 0.0f;
                            cloud.strength = 0.0f;
                            cloud.damage = creature->spellPower() * 0.1f;
                            cloud.ownerId = creature->m_id;
                            cloud.sizeInBlocks = 4.0f;
                            cloud.element = ElementWater;
                            cloud.position = origin;
                            cloud.visualSize = 1.0f;
                            cloud.currentTime = 0;
                            cloud.velocity = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
                            m_projectiles.push_back(cloud);
                            outgoing->projectiles.push_back(cloud);
                        }
                        hitAny = true;
                    }
                    origin += plasma::Vector<3, Fixed>(direction);
                }
                if (hitAny) {
                    creature->m_entityData.hitCombo++;
                    creature->onHit();
                    creature->m_entityData.timeSinceHit = 0;
                }
            }
            // A miss: a spark where the ray ends; the combo ends.
            if (struck.empty() && creature->m_entityData.currentAbility != AbilityBlockStrike) {
                ServerUpdates::ParticlePacket spark;
                spark.position = creature->m_entityData.position + plasma::Vector<3, Fixed>(length * aim.normalized());
                spark.count = RayMissParticleCount;
                spark.size = 0.1f;
                // Particle RGBA palette for this effect; channels and alpha stay together.
                spark.type = ParticleFire;
                spark.color = plasma::Vector<4, float>(1.0f, 0.2f, 0.5f);
                spark.acceleration = -direction;
                outgoing->particles.push_back(spark);
                float tempo = creature->tempo();
                creature->m_entityData.hitCombo = 0;
                time = (int)(tempo / creature->tempo() * (float)time);
            }
            if (creature->m_entityData.currentAbility == AbilityFireBeam && time <= creature->anticipationTime() &&
                time + milliseconds > creature->anticipationTime()) {
                ServerUpdates::SoundPacket sound;
                sound.position = creature->m_entityData.position;
                sound.sound = SoundMagic02;
                sound.pitch = (float)rand() * 0.1f / float(RAND_MAX) + 1.0f;
                outgoing->sounds.push_back(sound);
                break;
            }
            if (creature->m_entityData.currentAbility == AbilityBlockStrike && time <= creature->anticipationTime() &&
                time + milliseconds > creature->anticipationTime()) {
                ServerUpdates::SoundPacket sound;
                sound.position = creature->m_entityData.position;
                sound.pitch = (float)rand() * 0.25f / float(RAND_MAX) + 1.0f;
                sound.sound = hitAny ? SoundHit1 : SoundShieldSwing;
                outgoing->sounds.push_back(sound);
                break;
            }
            if (creature->m_entityData.currentAbility == AbilityFireBeam)
                break;
            if ((time + milliseconds) / 200 == time / 200)
                break;
            ServerUpdates::SoundPacket sound;
            sound.position = creature->m_entityData.position;
            sound.pitch = (float)rand() * 0.25f / float(RAND_MAX) + 1.0f;
            sound.sound = hitAny ? SoundChannelHit : SoundChannel2;
            outgoing->sounds.push_back(sound);
            break;
        }
        case AbilityScream:
            // A scream as the anticipation ends.
            if (m_client)
                break;
            if (creature->m_entityData.timeSinceAbility >= creature->anticipationTime() ||
                creature->m_entityData.timeSinceAbility + milliseconds < creature->anticipationTime())
                break;
            {
                ServerUpdates::SoundPacket sound;
                sound.position = creature->m_entityData.position;
                sound.sound = SoundLichScream;
                sound.pitch = (float)rand() * 0.25f / float(RAND_MAX) + 0.6f;
                outgoing->sounds.push_back(sound);
            }
            break;
        case AbilityPetFood: {
            // Eating offered pet food: when the creature it follows holds pet food for its race, each bite
            // adds a fifth to its buff 7; at full strength it becomes that creature's pet.
            if (m_client)
                break;
            if (creature->m_entityData.timeSinceAbility >= creature->anticipationTime() ||
                creature->m_entityData.timeSinceAbility + milliseconds < creature->anticipationTime())
                break;
            Creature *feeder = creatureByID(creature->m_opponentId);
            if (!feeder || feeder->m_entityData.equipment.pet.category != ItemCategoryPetFood ||
                feeder->m_entityData.equipment.pet.id != creature->m_entityData.race)
                break;
            ServerUpdates::SoundPacket sound;
            sound.position = creature->m_entityData.position;
            sound.sound = SoundDrink2;
            sound.pitch = (float)rand() * 0.2f / float(RAND_MAX) + 0.9f;
            outgoing->sounds.push_back(sound);
            HitPacket bite;
            bite.attackerId = creature->m_id;
            bite.targetId = feeder->m_id;
            bite.damage = 0.0f;
            bite.hitType = HitTypeTameness;
            bite.critical = 0;
            bite.position = creature->m_entityData.position;
            outgoing->hits.push_back(bite);
            float tameness = 0.0f;
            for (std::list<Creature::Buff>::iterator buff = creature->m_buffs.begin(); buff != creature->m_buffs.end();
                 buff++) {
                if (buff->type == BuffTameness)
                    tameness = buff->strength;
            }
            tameness += 0.2f;
            creature->m_entityData.hitCombo++;
            creature->m_entityData.timeSinceHit = 0;
            if (tameness >= 1.0f) {
                creature->m_entityData.hostilityType = HostilityPet;
                tameness = 1.0f;
                initCompanion(feeder->m_id, creature);
            }
            Creature::Buff buff;
            buff.type = BuffTameness;
            buff.timeRemaining = TamenessBuffDurationMs;
            buff.strength = tameness;
            buff.sourceId = creature->m_entityData.ownerId;
            applyBuff(creature, buff, outgoing);
            break;
        }
        case AbilityShoot:
        case AbilityChargedShotRelease:
        case AbilityThrowBoomerang: {
            // Shots: an arrow (22, 23) or the thrown weapon (26, a boomerang), as the anticipation ends. The
            // charge is spent then and powers the shot; a charged throw (26) flies again 300 ms later.
            if ((m_client || creature->m_entityData.hostilityType == HostilityPlayer) && creature != m_localPlayer)
                break;
            int &time = creature->m_entityData.timeSinceAbility;
            float &mana = creature->m_entityData.mp;
            if (time <= creature->anticipationTime() && time + milliseconds > creature->anticipationTime()) {
                if (creature->manaCost(creature->m_entityData.currentAbility, -1) > 0.0f)
                    mana -= creature->manaCost(creature->m_entityData.currentAbility, -1);
                else
                    mana -= creature->m_entityData.charge;
                if (0.0f > mana)
                    mana = 0.0f;
                creature->m_attackPower = creature->m_entityData.charge;
                creature->m_entityData.charge = 0.0f;
            }
            if (time > creature->anticipationTime() || time + milliseconds <= creature->anticipationTime()) {
                if (creature->m_entityData.currentAbility != AbilityThrowBoomerang || !(creature->m_attackPower > 0.0f))
                    break;
                if (time > creature->anticipationTime() + 300 ||
                    time + milliseconds <= creature->anticipationTime() + 300)
                    break;
            }
            BulletPacket shot;
            shot.velocity = 100.0f * creature->m_entityData.attackRotation.normalized();
            shot.currentTime = 0;
            shot.sizeInBlocks = 0.5f;
            if (creature->m_entityData.currentAbility == AbilityThrowBoomerang) {
                float strength = creature->m_attackPower;
                shot.knockback = strength * 0.2f;
                shot.sizeInBlocks = strength * 2.0f + 2.0f;
            } else {
                shot.knockback = creature->m_attackPower;
            }
            if (creature->manaCost(creature->m_entityData.currentAbility, -1) > 0.0f)
                shot.knockback = creature->manaCost(creature->m_entityData.currentAbility, -1);
            shot.strength = shot.knockback;
            shot.powered =
                creature->m_attackPower > 0.0f || creature->m_entityData.currentAbility == AbilityChargedShotRelease;
            plasma::Vector<3, Fixed> &position = creature->m_entityData.position;
            shot.position = position;
            shot.ownerId = creature->m_id;
            shot.home = creature->m_entityData.spawnSlot;
            // A powered shot uses up buff 10.
            if (shot.powered && creature->findBuff(BuffInstantShoot)) {
                Creature::Buff buff;
                buff.type = BuffInstantShoot;
                buff.timeRemaining = 0;
                creature->setBuff(buff);
                BuffPacket packet;
                packet.sourceId = creature->m_id;
                packet.targetId = creature->m_id;
                packet.buff = buff;
                outgoing->buffs.push_back(packet);
            }
            if (creature->m_entityData.currentAbility == AbilityThrowBoomerang) {
                shot.projectileType = ProjectileBoomerang;
                shot.visualSize = creature->m_entityData.appearance.weaponScale;
            } else {
                shot.projectileType = ProjectileArrow;
            }
            float growth = 5.0f;
            float base = 1.0f;
            if (creature->m_entityData.currentAbility == AbilityThrowBoomerang)
                growth = 1.0f;
            if (shot.knockback > 0.0f)
                base = 0.1f;
            if (creature->m_entityData.currentAbility == AbilityChargedShotRelease)
                base = 0.0f;
            float charge = creature->m_attackPower;
            float damage = creature->damageScale(creature->m_entityData.currentAbility) * creature->attackPower() *
                           (charge * charge * growth + base);
            float variance = 0.0f;
            shot.damage = (1.25f - (float)rand() * 0.5f / float(RAND_MAX) + variance) * damage;
            m_projectiles.push_back(shot);
            outgoing->projectiles.push_back(shot);
            ServerUpdates::SoundPacket sound;
            sound.position = position;
            int ability = creature->m_entityData.currentAbility;
            if (ability == AbilityShoot || ability == AbilityChargedShotRelease) {
                float roll = (float)rand() * 0.5f / float(RAND_MAX);
                if (shot.powered) {
                    sound.pitch = roll + 1.0f;
                    sound.sound = SoundSalvo2;
                } else {
                    sound.pitch = roll + 2.0f;
                    sound.sound = SoundSwing;
                }
            } else if (ability == AbilityThrowBoomerang) {
                sound.pitch = (float)((double)rand() * 0.51 / double(RAND_MAX) + 0.7f);
                sound.sound = SoundSwing;
            } else {
                sound.sound = SoundSalvo2;
                sound.pitch = (float)rand() * 0.5f / float(RAND_MAX) + 1.0f;
            }
            outgoing->sounds.push_back(sound);
            break;
        }
        case AbilityRetreat:
        case AbilitySalvoRelease:
        case AbilityShuriken: {
            // A volley: four powered arrows over the strike, the charge spent as the anticipation ends.
            if ((m_client || creature->m_entityData.hostilityType == HostilityPlayer) && creature != m_localPlayer)
                break;
            if (creature->m_entityData.timeSinceAbility <= creature->anticipationTime() &&
                creature->m_entityData.timeSinceAbility + milliseconds > creature->anticipationTime()) {
                creature->m_attackPower = creature->m_entityData.charge;
                creature->m_entityData.mp -= creature->m_entityData.charge;
                creature->m_entityData.charge = 0.0f;
                if (creature->findBuff(BuffInstantShoot)) {
                    Creature::Buff buff;
                    buff.type = BuffInstantShoot;
                    buff.timeRemaining = 0;
                    creature->setBuff(buff);
                    BuffPacket packet;
                    packet.sourceId = creature->m_id;
                    packet.targetId = creature->m_id;
                    packet.buff = buff;
                    outgoing->buffs.push_back(packet);
                }
            }
            int period = creature->strikeTime() / 4;
            if (creature->m_entityData.timeSinceAbility < creature->anticipationTime())
                break;
            int time = creature->m_entityData.timeSinceAbility;
            if (time >= creature->anticipationTime() + creature->strikeTime() ||
                (time + milliseconds) / period == time / period)
                break;
            plasma::Vector<3, float> &aim = creature->m_entityData.attackRotation;
            if (!(aim.squaredLength() > 0.0f))
                break;
            plasma::Vector<3, Fixed> &position = creature->m_entityData.position;
            ServerUpdates::SoundPacket sound;
            sound.position = position;
            sound.sound = SoundSalvo2;
            sound.pitch = (float)rand() * 0.5f / float(RAND_MAX) + 1.0f;
            outgoing->sounds.push_back(sound);
            BulletPacket arrow;
            arrow.velocity = 150.0f * aim.normalized();
            arrow.currentTime = 0;
            arrow.sizeInBlocks = 0.5f;
            arrow.knockback = creature->m_attackPower * 0.25f;
            arrow.strength = 0.0f;
            arrow.powered = 1;
            arrow.position = position;
            arrow.ownerId = creature->m_id;
            arrow.home = creature->m_entityData.spawnSlot;
            arrow.projectileType = ProjectileArrow;
            float base = 1.0f;
            if (creature->m_entityData.currentAbility == AbilitySalvoRelease)
                base = 0.1f;
            float charge = creature->m_attackPower;
            float damage = creature->damageScale(creature->m_entityData.currentAbility) * creature->attackPower() *
                           (charge * charge * 5.0f + base);
            float variance = 0.0f;
            arrow.damage = (1.25f - (float)rand() * 0.5f / float(RAND_MAX) + variance) * damage;
            m_projectiles.push_back(arrow);
            outgoing->projectiles.push_back(arrow);
            break;
        }
        case AbilityFireball:
        case AbilityWandFirebolt:
        case AbilityFirebolt:
        case AbilityBraceletFirebolt:
        case AbilityWaterDrop:
        case AbilityBraceletWaterDrop:
        case AbilityWaterSplash:
        case AbilityWandWaterDrop:
        case AbilityWaterSalvo:
        case AbilityFireSalvo:
        case AbilityThrowRock: {
            // Spells: a magic projectile (fire or water by the mage's specialization) as the anticipation
            // ends; 45 and 46 cast three over the strike, 108 throws a heavier projectile of type 4. The
            // charge widens the projectile and is spent with it.
            if ((m_client || creature->m_entityData.hostilityType == HostilityPlayer) && creature != m_localPlayer)
                break;
            int time = creature->m_entityData.timeSinceAbility;
            if (time <= creature->anticipationTime() - 200 && (time + milliseconds) / 200 != time / 200) {
                ServerUpdates::SoundPacket sound;
                sound.position = creature->m_entityData.position;
                sound.sound = SoundChannel2;
                sound.pitch = (float)rand() * 0.25f / float(RAND_MAX) + 1.0f;
                outgoing->sounds.push_back(sound);
            }
            int castAt = creature->anticipationTime();
            if (creature->m_entityData.currentAbility == AbilityFireSalvo ||
                creature->m_entityData.currentAbility == AbilityWaterSalvo) {
                int third = creature->strikeTime() / 3;
                castAt =
                    third * ((creature->m_entityData.timeSinceAbility + milliseconds - creature->anticipationTime()) /
                             third) +
                    creature->anticipationTime();
                if (castAt < creature->anticipationTime())
                    castAt = creature->anticipationTime();
            }
            plasma::Vector<3, float> &aim = creature->m_entityData.attackRotation;
            float &mana = creature->m_entityData.mp;
            if (creature->m_entityData.timeSinceAbility <= creature->anticipationTime() &&
                creature->m_entityData.timeSinceAbility + milliseconds > creature->anticipationTime() &&
                aim.squaredLength() > 0.0f) {
                mana -= creature->manaCost(creature->m_entityData.currentAbility, -1);
                if (0.0f > mana)
                    mana = 0.0f;
            }
            time = creature->m_entityData.timeSinceAbility;
            if (time >= creature->anticipationTime() + creature->strikeTime() || time > castAt ||
                time + milliseconds <= castAt)
                break;
            if (!(aim.squaredLength() > 0.0f))
                break;
            BulletPacket spell;
            // A bracelet casts twice as fast as other weapons.
            if (creature->m_entityData.equipment.weaponRight.id == WeaponBracelet)
                spell.velocity = 100.0f * aim.normalized();
            else
                spell.velocity = 50.0f * aim.normalized();
            spell.currentTime = 0;
            float &charge = creature->m_entityData.charge;
            spell.sizeInBlocks = charge * 4.0f + 0.5f;
            plasma::Vector<3, Fixed> &position = creature->m_entityData.position;
            spell.position = position;
            // Charge widens the bolt and increases its push.
            spell.knockback = charge;
            spell.visualSize = charge + 0.5f;
            // Race 101 casts from its front, half its height up.
            if (creature->m_entityData.race == RaceDragon) {
                plasma::Vector<3, float> &size = creature->m_entityData.appearance.size;
                spell.position[1] += Fixed(size[1]);
                spell.position[2] += Fixed(size[2] * 0.5f);
            }
            spell.ownerId = creature->m_id;
            spell.home = creature->m_entityData.spawnSlot;
            float variance = 0.0f;
            if (creature->m_entityData.currentAbility == AbilityThrowRock) {
                spell.projectileType = ProjectileRock;
                float damage = creature->damageScale(creature->m_entityData.currentAbility) * creature->attackPower();
                spell.visualSize = 4.0f;
                spell.knockback = 1.0f;
                spell.powered = 1;
                spell.damage = (1.25f - (float)rand() * 0.5f / float(RAND_MAX)) * damage * (variance + 1.0f) * 5.0f;
                spell.velocity = 100.0f * aim.normalized();
            } else {
                spell.projectileType = ProjectileMagic;
                int ability = creature->m_entityData.currentAbility;
                if (ability == AbilityWandFirebolt || ability == AbilityFirebolt ||
                    ability == AbilityBraceletFirebolt || ability == AbilityWandWaterDrop ||
                    ability == AbilityWaterDrop || ability == AbilityBraceletWaterDrop) {
                    float damage =
                        creature->damageScale(creature->m_entityData.currentAbility) * creature->spellPower();
                    spell.damage = (1.25f - (float)rand() * 0.5f / float(RAND_MAX)) * damage * (variance + 1.0f);
                } else if (ability == AbilityFireSalvo || ability == AbilityWaterSalvo) {
                    float damage =
                        creature->damageScale(creature->m_entityData.currentAbility) * creature->spellPower();
                    spell.knockback = 0.25f;
                    spell.damage = (1.25f - (float)rand() * 0.5f / float(RAND_MAX)) * damage * (variance + 1.0f);
                    spell.visualSize += 0.5f;
                } else {
                    float damage =
                        creature->damageScale(creature->m_entityData.currentAbility) * creature->spellPower();
                    spell.knockback = 1.0f;
                    spell.damage = (1.25f - (float)rand() * 0.5f / float(RAND_MAX)) * damage * (variance + 1.0f);
                    spell.visualSize += 1.0f;
                }
            }
            if (creature->m_entityData.classType == ClassMage)
                spell.element =
                    creature->m_entityData.specialization == SpecializationWater ? ElementWater : ElementFire;
            int ability = creature->m_entityData.currentAbility;
            spell.powered = ability == AbilityFireSalvo || ability == AbilityWaterSalvo || ability == AbilityFireball ||
                            ability == AbilityWaterSplash;
            m_projectiles.push_back(spell);
            if (charge > 0.0f) {
                mana -= charge;
                if (0.0f > mana)
                    mana = 0.0f;
                charge = 0.0f;
            }
            // A powered spell uses up buff 9.
            if (spell.powered && creature->findBuff(BuffInstantFire)) {
                Creature::Buff buff;
                buff.type = BuffInstantFire;
                buff.timeRemaining = 0;
                creature->setBuff(buff);
                BuffPacket packet;
                packet.sourceId = creature->m_id;
                packet.targetId = creature->m_id;
                packet.buff = buff;
                outgoing->buffs.push_back(packet);
            }
            outgoing->projectiles.push_back(spell);
            ServerUpdates::SoundPacket sound;
            sound.position = position;
            sound.pitch = 1.0f;
            sound.volume = 0.9f;
            ability = creature->m_entityData.currentAbility;
            if (spell.element == ElementWater) {
                sound.sound = SoundWatersplash;
                if (ability == AbilityWandWaterDrop || ability == AbilityWaterDrop ||
                    ability == AbilityBraceletWaterDrop)
                    sound.pitch = 1.25f;
            } else {
                sound.sound = SoundFireball;
                if (ability == AbilityWandFirebolt || ability == AbilityFirebolt || ability == AbilityBraceletFirebolt)
                    sound.pitch = 2.0f;
            }
            outgoing->sounds.push_back(sound);
            break;
        }
        default:
            break;
        }

        // Four seconds after the last hit the combo ends, the attack is rescaled and held cooldowns
        // restart; free cooldowns run down.
        if (!m_client || creature == m_localPlayer) {
            creature->m_entityData.timeSinceHit += milliseconds;
            if (creature->m_entityData.timeSinceHit > 4000) {
                float tempo = creature->tempo();
                creature->m_entityData.hitCombo = 0;
                creature->m_entityData.timeSinceAbility =
                    (int)(tempo / creature->tempo() * (float)creature->m_entityData.timeSinceAbility);
                for (std::map<int, int>::iterator cooldown = creature->m_skillCooldowns.begin();
                     cooldown != creature->m_skillCooldowns.end(); cooldown++) {
                    if (creature->holdsCooldown(cooldown->first))
                        cooldown->second = creature->cooldown(cooldown->first, -1);
                }
            }
        }
        for (std::map<int, int>::iterator cooldown = creature->m_skillCooldowns.begin();
             cooldown != creature->m_skillCooldowns.end(); cooldown++) {
            if (!creature->holdsCooldown(cooldown->first)) {
                cooldown->second -= milliseconds;
                if (cooldown->second < 0)
                    cooldown->second = 0;
            }
        }
        // The buffs run out; a bulwark ends a stun, poison hurts every 400 ms.
        std::list<std::list<Creature::Buff>::iterator> expired;
        for (std::list<Creature::Buff>::iterator it2 = creature->m_buffs.begin(); it2 != creature->m_buffs.end();
             it2++) {
            Creature::Buff &buff = *it2;
            buff.timeRemaining -= milliseconds;
            if (buff.timeRemaining <= 0) {
                expired.push_back(it2);
                continue;
            }
            switch (buff.type) {
            case BuffBulwark:
                if (creature->m_entityData.stunTime > 0)
                    creature->m_entityData.stunTime = 0;
                break;
            case BuffPoisoned:
                if (!m_client && (buff.timeRemaining + milliseconds) / 400 != buff.timeRemaining / 400) {
                    Creature *source = creatureByID(buff.sourceId);
                    attackCreature(creature, source, buff.strength, false, false, 0.0f, creature->m_abilityTarget,
                                   plasma::Vector<3, float>(0.0f, 0.0f, 0.0f), outgoing, &changedZones, true, 0, 0,
                                   false);
                }
                break;
            }
        }
        for (std::list<std::list<Creature::Buff>::iterator>::iterator it2 = expired.begin(); it2 != expired.end();
             it2++)
            creature->m_buffs.erase(*it2);

        // A stun interrupts the ability.
        creature->m_entityData.stunTime -= milliseconds;
        if (creature->m_entityData.stunTime > 0) {
            creature->m_entityData.timeSinceAbility = 0;
            creature->m_entityData.currentAbility = AbilityNone;
        }
        // The ability's clock; its cooldown starts with it. A dive (54) strikes downwards.
        if (creature->m_entityData.hostilityType != HostilityAttackableObject &&
            (creature->m_entityData.timeSinceAbility >= 0 || creature->m_entityData.currentAbility == AbilityNone)) {
            if (creature->m_entityData.timeSinceAbility == 0) {
                int ability = creature->m_entityData.currentAbility;
                creature->m_skillCooldowns[ability] = creature->cooldown(ability, -1);
            }
            creature->m_entityData.timeSinceAbility += milliseconds;
            if (creature->m_entityData.currentAbility == AbilitySmash &&
                creature->m_entityData.timeSinceAbility > creature->anticipationTime() &&
                creature->m_entityData.timeSinceAbility < creature->attackDuration() &&
                creature->m_entityData.timeSinceAbility < creature->anticipationTime() + creature->strikeTime())
                creature->m_entityData.velocity[2] = -60.0f;
        }
        // When an attack is over, the creatures that dodged it without being struck get their reward and a
        // dodge hit.
        if (creature->m_entityData.currentAbility == AbilityNone) {
            for (std::set<long long>::iterator dodger = creature->m_dodgingCreatures.begin();
                 dodger != creature->m_dodgingCreatures.end(); dodger++) {
                if (creature->m_struckCreatures.find(*dodger) != creature->m_struckCreatures.end())
                    continue;
                Creature *target = creatureByID(*dodger);
                if (!target)
                    continue;
                target->applyDodgeBonus(outgoing);
                HitPacket hit;
                hit.damage = 0.0f;
                hit.attackerId = creature->m_id;
                hit.targetId = target->m_id;
                hit.hitType = HitTypeDodge;
                hit.critical = 0;
                hit.position = target->m_entityData.position;
                outgoing->hits.push_back(hit);
            }
            creature->m_struckCreatures.clear();
            creature->m_dodgingCreatures.clear();
        }

        // Mana: a caster's grows except while casting, the others' drains except while charging or
        // blocking.
        int ability = creature->m_entityData.currentAbility;
        float &mana = creature->m_entityData.mp;
        if (creature->isCaster()) {
            if (ability != AbilitySupportRay &&
                !((ability == AbilityFireRay || ability == AbilityFireball || ability == AbilityFireSalvo ||
                   ability == AbilityWaterSalvo || ability == AbilityFireVortex || ability == AbilityWaterVortex ||
                   ability == AbilityWaterSplash || ability == AbilityHealingStream) &&
                  creature->m_entityData.timeSinceAbility < creature->attackDuration()))
                mana += elapsed * 0.0001f;
        } else if (ability != AbilityChargedShot && ability != AbilitySalvo && ability != AbilityChargeBoomerang &&
                   ability != AbilityCrush && ability != AbilitySwirl && ability != AbilityMonsterCharge &&
                   ability != AbilityBlock) {
            mana -= elapsed * 5e-05f;
        }
        if (0.0f > mana)
            mana = 0.0f;
        if (mana > 1.0f)
            mana = 1.0f;
        // Blocking spends block power (half as fast with a shield), which otherwise recovers in two
        // seconds.
        float &blockPower = creature->m_entityData.blockPower;
        if (creature->isBlocking()) {
            if (blockPower > 0.0f) {
                Item &shield = creature->m_entityData.equipment.weaponLeft;
                if (shield.category == ItemCategoryWeapon && shield.id == WeaponShield)
                    blockPower -= elapsed / 1200.0f;
                else
                    blockPower -= elapsed / 600.0f;
                if (0.0f > blockPower)
                    blockPower = 0.0f;
            }
        } else {
            blockPower += elapsed / 2000.0f;
        }
        if (blockPower > 1.0f)
            blockPower = 1.0f;
        if (creature->m_entityData.currentAbility != AbilityNone)
            creature->m_entityData.setToggles((1 << CreatureToggleGliding), false);
        if (creature->m_entityData.acceleration.squaredLength() > 0.0f)
            creature->m_entityData.setToggles((1 << CreatureToggleSneaking), false);
        // Moving gets one up from a seat or a bed.
        if ((creature->m_entityData.currentAbility == AbilitySit ||
             creature->m_entityData.currentAbility == AbilitySleep) &&
            creature->m_entityData.acceleration.squaredLength() > 0.0f)
            creature->m_entityData.currentAbility = AbilityNone;
        // Drinking or eating heals and regenerates in 200 ms steps over the item's cooldown (half as much
        // for computer creatures).
        if (((!m_client && creature->m_entityData.hostilityType != HostilityPlayer) || creature == m_localPlayer) &&
            (creature->m_entityData.currentAbility == AbilityDrink ||
             creature->m_entityData.currentAbility == AbilityEat)) {
            Item &item = creature->m_entityData.actionItem;
            int time = creature->m_entityData.timeSinceAbility;
            int before = time - milliseconds;
            if (before < item.cooldown() && before / 200 != time / 200) {
                float share = 200.0f / (float)item.cooldown();
                if (creature->m_entityData.hostilityType != HostilityPlayer)
                    share *= 0.5f;
                float healing = item.effectValue() * share;
                if (healing > 0.0f) {
                    HitPacket heal;
                    heal.position = creature->m_entityData.position;
                    heal.targetId = creature->m_id;
                    heal.attackerId = -1;
                    heal.damage = -healing;
                    heal.critical = 0;
                    outgoing->hits.push_back(heal);
                    creature->m_entityData.hp += healing;
                    if (creature->m_entityData.hp > creature->maxHP())
                        creature->m_entityData.hp = creature->maxHP();
                }
                float regeneration =
                    item.regenerationValue() / powf(2.0f, (float)creature->m_entityData.level * 0.25f) * share;
                float &recovering = creature->m_entityData.revivalPenalty;
                recovering += regeneration;
                if (recovering >= 0.0f) {
                    if (0.0f > recovering - regeneration) {
                        ServerUpdates::SoundPacket sound;
                        sound.sound = SoundUpgrade2;
                        sound.position = creature->m_entityData.position;
                        outgoing->sounds.push_back(sound);
                    }
                    recovering = 0.0f;
                }
                if ((creature->m_entityData.timeSinceAbility - milliseconds) / ConsumptionSoundPeriodMs !=
                    creature->m_entityData.timeSinceAbility / ConsumptionSoundPeriodMs) {
                    ServerUpdates::SoundPacket sound;
                    sound.position = creature->m_entityData.position;
                    sound.pitch = 1.0f;
                    sound.sound = SoundDrink2;
                    outgoing->sounds.push_back(sound);
                }
            }
        }

        // Aggro fades (to at least 0.01 for a living target); targets that died, and spent aggro, are
        // forgotten. Hit counts fade likewise.
        std::list<std::map<long long, float>::iterator> forgotten;
        float fade = elapsed * 0.00025f;
        for (std::map<long long, float>::iterator aggro = creature->m_aggro.begin(); aggro != creature->m_aggro.end();
             aggro++) {
            float weight = aggro->second - fade;
            aggro->second = weight;
            Creature *target = creatureByID(aggro->first);
            if (!target)
                continue;
            if (target->m_entityData.hp > 0.0f && 0.01f > weight)
                aggro->second = 0.01f;
            if (0.0f >= aggro->second || (aggro->first != 0 && 0.0f >= target->m_entityData.hp))
                forgotten.push_back(aggro);
        }
        for (std::list<std::map<long long, float>::iterator>::iterator it2 = forgotten.begin(); it2 != forgotten.end();
             it2++)
            creature->m_aggro.erase(*it2);
        std::list<std::map<long long, float>::iterator> faded;
        float none = 0.0f;
        for (std::map<long long, float>::iterator count = creature->m_hitCounts.begin();
             count != creature->m_hitCounts.end(); count++) {
            blend(count->second, none, milliseconds, 0.001f);
            Creature *target = creatureByID(count->first);
            if (!target || 0.0f >= target->m_entityData.hp)
                faded.push_back(count);
        }
        for (std::list<std::map<long long, float>::iterator>::iterator it2 = faded.begin(); it2 != faded.end(); it2++)
            creature->m_hitCounts.erase(*it2);
        // An enemy nobody fights is healed at once.
        if (!m_client && creature->m_entityData.hostilityType == HostilityEnemy && creature->m_entityData.hp > 0.0f &&
            creature->m_aggro.empty())
            creature->m_entityData.hp = creature->maxHP();

        // Movement: this step's share of the acceleration, none while stunned, no rising under
        // CreatureFlagPartlyAboveWater unless climbing.
        bool canStepUp = true;
        plasma::Vector<3, float> step = seconds * creature->m_entityData.acceleration;
        if (creature->m_entityData.stunTime > 0)
            step = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
        if (!creature->m_entityData.binaryToggles.climbing &&
            (creature->m_entityData.flags & (1 << CreatureFlagPartlyAboveWater))) {
            float &rise = step[2];
            if (rise > 0.0f)
                rise = 0.0f;
        }
        // Leaning into turns: half the angle between the velocity and the push, in degrees (more for small
        // creatures, much more gliding).
        plasma::Vector<2, float> push(step[0], step[1]);
        float lean = 0.0f;
        if ((double)push.squaredLength() > 0.01) {
            plasma::Vector<2, float> heading(creature->m_entityData.velocity[0], creature->m_entityData.velocity[1]);
            if (heading.squaredLength() > 0.01f) {
                push.normalize();
                heading.normalize();
                float cross = heading[1] * push[0] - heading[0] * push[1];
                if (-1.0f > cross)
                    cross = -1.0f;
                else if (cross > 1.0f)
                    cross = 1.0f;
                lean = (float)(asinf(cross) / 3.141592653589793 * 180.0 * 0.5);
            }
        }
        plasma::Vector<3, float> &size = creature->m_entityData.appearance.size;
        float &width = size[0];
        if (width > 2.0f)
            lean *= 2.0f / width;
        float &roll = creature->m_entityData.rotation[1];
        if (creature->isGliding()) {
            lean *= 40.0f;
            if (lean > 90.0f)
                lean = 90.0f;
            else if (-90.0f > lean)
                lean = -90.0f;
            blend(roll, lean, milliseconds, 0.001f);
        } else {
            blend(roll, lean, milliseconds, 0.005f);
        }
        plasma::Vector<2, float> velocity(creature->m_entityData.velocity[0], creature->m_entityData.velocity[1]);

        // The speed: 6 blocks per second, 8 for bosses and tinted enemies, 12 for pets, scaled by the
        // skills for swimming (and its ability 107), riding, hang gliding and climbing.
        float speed = 6.0f;
        if (creature->isBoss() || (creature->m_entityData.appearance.appearanceFlags & (1 << AppearanceFlagTinted)))
            speed = 8.0f;
        if (creature->m_entityData.hostilityType == HostilityPet)
            speed = 12.0f;
        if (creature->m_entityData.currentAbility == AbilitySailBoat) {
            if (creature->m_entityData.flags & (1 << CreatureFlagInWater))
                speed *= bonusFromLevel(creature->m_entityData.skillLevels[SkillBoatDriving]);
        } else if (creature->m_entityData.flags & (1 << CreatureFlagInWater)) {
            speed *= swimmingSpeed(creature->m_entityData.skillLevels[SkillSwimming]);
        }
        if (creature == m_localPlayer) {
            if (creature->m_entityData.currentAbility == AbilitySailBoat &&
                (creature->m_entityData.flags & (1 << CreatureFlagOnGround)) &&
                !(creature->m_entityData.flags & (1 << CreatureFlagInWater)))
                creature->m_entityData.currentAbility = AbilityNone;
            // Riding the companion (ability 106) needs a living pet and the riding skill, and costs riding
            // stamina while moving.
            if (creature == m_localPlayer && creature->m_entityData.currentAbility == AbilityRidePet) {
                long long &companionId = creature->m_companionId;
                if (creatureByID(companionId) && creatureByID(companionId)->m_entityData.hp >= 0.0f &&
                    isPet(creatureByID(companionId)->m_entityData.race)) {
                    float riding = ridingSpeed(creature->m_entityData.skillLevels[SkillPetRiding]);
                    if (riding == 0.0f)
                        creature->m_entityData.currentAbility = AbilityNone;
                    else
                        speed *= riding;
                    if (!creatureByID(companionId))
                        creature->m_entityData.currentAbility = AbilityNone;
                } else if (creature == m_localPlayer) {
                    creature->m_entityData.currentAbility = AbilityNone;
                }
                if (m_localPlayer->m_entityData.acceleration.squaredLength() > 0.0f)
                    creature->m_ridingStamina -= seconds * 0.002f;
                if (0.0f >= creature->m_ridingStamina) {
                    creature->m_ridingStamina = 0.0f;
                    creature->m_entityData.currentAbility = AbilityNone;
                }
                if (creature->m_entityData.binaryToggles.climbing)
                    creature->m_entityData.currentAbility = AbilityNone;
            }
        }
        // A debugging switch of the world (m_debugFlags bit 0) makes the local player fast.
        if (creature == m_localPlayer && (m_debugFlags & (1 << WorldDebugFastMovement)))
            speed = 20.0f;
        if (creature->m_entityData.binaryToggles.gliding) {
            float gliding = hangGlidingSpeed(creature->m_entityData.skillLevels[SkillHangGliding]);
            if (gliding > 0.0f) {
                if (!(creature->m_entityData.flags & (1 << CreatureFlagOnGround)))
                    speed *= gliding;
            } else {
                creature->m_entityData.binaryToggles.gliding = 0;
            }
        }
        // Climbing costs stamina by the lack of climbing skill; at 1 % the creature lets go.
        if (creature->m_entityData.binaryToggles.climbing &&
            creature->m_entityData.acceleration.squaredLength() > 0.0f) {
            float climbing = climbingSpeed(creature->m_entityData.skillLevels[SkillClimbing]);
            if (climbing > 0.0f) {
                if (!creature->m_entityData.binaryToggles.gliding &&
                    (creature->m_entityData.flags & (1 << CreatureFlagAtWall)) &&
                    !(creature->m_entityData.flags & (1 << CreatureFlagOnGround))) {
                    creature->m_stamina -= (1.0f - climbing) * 0.0005f * elapsed;
                    if (0.0f > creature->m_stamina)
                        creature->m_stamina = 0.0f;
                    if (0.01f > creature->m_stamina)
                        creature->m_entityData.binaryToggles.climbing = 0;
                }
            } else {
                creature->m_entityData.binaryToggles.climbing = 0;
            }
        }
        // Stealth (ability 79, the stealth factor m_stealth): slower, and lost by light (the zone's
        // lights, street lights and lamps lying about within 16 blocks, lit lamps within 10), by day and
        // by moving; sneaking (toggle bit 10) builds it, anything else loses it.
        float &stealth = creature->m_stealth;
        if (creature->m_entityData.currentAbility == AbilitySneak) {
            speed *= creature->abilitySkillBonus(AbilitySneak, -1) * 0.4f + 0.5f;
            float daytime = powf((float)getTime() * 2.0f / float(24 * World::MillisecondsPerHour) - 1.0f, 2.0f);
            float darkness = powf(1.0f - daytime, 2.0f);
            plasma::Vector<3, Fixed> &position = creature->m_entityData.position;
            float light = lightLevel(position) * darkness;
            for (std::set<Zone *>::iterator it2 = playerZones.begin(); it2 != playerZones.end(); it2++) {
                Zone *const &zone = *it2;
                for (std::list<Zone::Plant>::iterator source = zone->m_plants.begin(); source != zone->m_plants.end();
                     source++) {
                    if (!(source->flags & (1 << PlantFlagEmissive)))
                        continue;
                    float distance = plasma::Vector<3, float>(source->position - position).squaredLength();
                    if (256.0f > distance)
                        light += (1.0f - distance * 0.00390625f) * (1.0f - distance * 0.00390625f) * 0.5f;
                }
                for (std::vector<Zone::Object>::iterator object = zone->m_objects.begin();
                     object != zone->m_objects.end(); object++) {
                    if (object->type != StaticObjectStreetLight)
                        continue;
                    float distance = plasma::Vector<3, float>(object->position - position).squaredLength();
                    if (256.0f > distance)
                        light += (1.0f - distance * 0.00390625f) * (1.0f - distance * 0.00390625f) * 0.5f;
                }
                for (std::vector<Zone::ZoneItem>::iterator item = zone->m_items.begin(); item != zone->m_items.end();
                     item++) {
                    if (item->item.category != ItemCategoryCandle)
                        continue;
                    float distance = plasma::Vector<3, float>(item->position - position).squaredLength();
                    if (256.0f > distance)
                        light += (1.0f - distance * 0.00390625f) * (1.0f - distance * 0.00390625f) * 0.5f;
                }
            }
            if (light > 1.0f)
                light = 1.0f;
            for (std::map<long long, Creature *>::iterator other = m_creatures.begin(); other != m_creatures.end();
                 other++) {
                Creature *carrier = other->second;
                if (!carrier || !carrier->m_entityData.binaryToggles.lampOn)
                    continue;
                float distance = plasma::Vector<3, float>(carrier->m_entityData.position - position).squaredLength();
                if (100.0f > distance)
                    light += (1.0f - distance / 100.0f) * (1.0f - distance / 100.0f);
            }
            float exposure = 1.0f - (0.9f - creature->abilitySkillBonus(AbilitySneak, -1) * 0.5f) * light;
            float moving = 0.5f - creature->abilitySkillBonus(AbilitySneak, -1) * 0.5f;
            stealth += (exposure - moving * creature->m_entityData.velocity.length()) * (elapsed * 0.0005f);
            if (0.0f > stealth)
                stealth = 0.0f;
            if (stealth > 1.0f)
                stealth = 1.0f;
        } else if (creature->m_entityData.binaryToggles.sneaking) {
            stealth += (creature->abilitySkillBonus(AbilityAim, -1) * 0.0005f + 1e-05f) * elapsed;
            if (stealth > 1.0f)
                stealth = 1.0f;
        } else {
            stealth -= elapsed * 0.0005f;
            if (0.0f > stealth)
                stealth = 0.0f;
        }
        // Buff 3 keeps it full; mana grows with it.
        if (creature->findBuff(BuffCamouflage))
            stealth = 1.0f;
        creature->m_entityData.mp += elapsed * 0.00025f * stealth;
        if (creature->m_entityData.mp > 1.0f)
            creature->m_entityData.mp = 1.0f;
        // Stamina: fast gliding spends it, it recovers except while gliding or climbing.
        Creature::BinaryToggles &toggles = creature->m_entityData.binaryToggles;
        unsigned int flags = creature->m_entityData.flags;
        if (toggles.gliding && !(flags & (1 << CreatureFlagOnGround))) {
            if (toggles.fastMovement)
                creature->m_stamina -= elapsed * 0.0002f;
        } else if (!(toggles.climbing && !toggles.gliding && (flags & (1 << CreatureFlagAtWall)) &&
                     !(flags & (1 << CreatureFlagOnGround)))) {
            creature->m_stamina += elapsed * 0.0001f;
        }
        if (0.0f > creature->m_stamina)
            creature->m_stamina = 0.0f;
        if (creature->m_stamina > 1.0f)
            creature->m_stamina = 1.0f;
        // Fast movement doubles the speed, except climbing or in the air.
        if (toggles.fastMovement && !(toggles.climbing && (flags & (1 << CreatureFlagAtWall))) &&
            !(toggles.gliding && !(flags & (1 << CreatureFlagOnGround))))
            speed = (float)(speed * 2.0);
        if (creature->findBuff(BuffTempoBoost))
            speed *= creature->abilitySkillBonus(AbilitySwiftness, -1) + 1.0f;
        // Charges and dashes rush at 40 blocks per second.
        int currentMovementAbility = creature->m_entityData.currentAbility;
        if (currentMovementAbility == AbilityIntercept ||
            (currentMovementAbility == AbilitySmash &&
             creature->m_entityData.timeSinceAbility < creature->anticipationTime() + creature->strikeTime()) ||
            ((currentMovementAbility == AbilityRetreat || currentMovementAbility == AbilityShuriken) &&
             creature->m_entityData.timeSinceAbility < RetreatRushDurationMs))
            speed = 40.0f;
        // The first 200 ms of a melee attack (and all of a charge) carry the attacker forward, until it
        // meets a hostile creature or after half a second.
        bool blocked = false;
        bool lunging =
            currentMovementAbility == AbilityIntercept ||
            ((currentMovementAbility == AbilityTwoHandedThrust || currentMovementAbility == AbilityTwoHandedSpin ||
              currentMovementAbility == AbilityTwoHandedSlam || currentMovementAbility == AbilityTwoHandedSwing ||
              currentMovementAbility == AbilityCrossedWeaponSweep ||
              currentMovementAbility == AbilityMonsterChargeStrike || currentMovementAbility == Ability3 ||
              currentMovementAbility == Ability62 || currentMovementAbility == AbilitySwirlStrike ||
              currentMovementAbility == Ability4 || currentMovementAbility == AbilityRightSlash ||
              currentMovementAbility == AbilityShieldAttack || currentMovementAbility == AbilityLeftSlash ||
              currentMovementAbility == AbilityRightStab || currentMovementAbility == AbilityLeftStab ||
              currentMovementAbility == AbilityRightSweep || currentMovementAbility == AbilityLeftSweep ||
              currentMovementAbility == AbilityOverheadChop || currentMovementAbility == AbilityKick ||
              currentMovementAbility == AbilityRightDaggerPierce || currentMovementAbility == AbilityLeftDaggerPierce ||
              currentMovementAbility == AbilityAmbush || currentMovementAbility == AbilityPerforate ||
              currentMovementAbility == AbilityBlockJab) &&
             creature->m_entityData.timeSinceAbility < 200);
        if ((creature == m_localPlayer || !m_client) && lunging) {
            for (std::map<long long, Creature *>::iterator other = m_creatures.begin(); other != m_creatures.end();
                 other++) {
                Creature *&target = other->second;
                if (!target || target == creature || !(target->m_entityData.hp > 0.0f) || !isHostile(creature, target))
                    continue;
                float reach = target->m_entityData.appearance.size[0] * 0.7f + (width + 1.0f);
                plasma::Vector<3, Fixed> offset = creature->m_entityData.position - target->m_entityData.position;
                if (!(plasma::Vector<2, Fixed>(offset[0], offset[1]).squaredLength() < reach * reach))
                    continue;
                // Both height operands come from the attacker, so the difference is zero. any hostile creature within horizontal reach stops the charge regardless of height.
                Fixed &z = creature->m_entityData.position[2];
                float height = fabsf((float)(z - z));
                if (target->m_entityData.appearance.size[2] * 0.5f + size[2] + 1.0f > height)
                    blocked = true;
            }
            if (blocked || creature->m_entityData.timeSinceAbility > 500) {
                // A charge ends in the weapon's follow-up attack.
                if (creature->m_entityData.currentAbility == AbilityIntercept) {
                    creature->m_attackPower = 0;
                    creature->m_entityData.mp = 1.0f;
                    creature->m_entityData.currentAbility = AbilityNone;
                    creature->m_entityData.timeSinceAbility = 0;
                    Item &weapon = creature->m_entityData.equipment.weaponRight;
                    if (weapon.category == ItemCategoryWeapon) {
                        if (weapon.id == WeaponLongsword)
                            creature->m_entityData.currentAbility = AbilityPerforate;
                        else
                            creature->m_entityData.currentAbility =
                                weapon.id == WeaponDagger ? AbilityAmbush : AbilityKick;
                    }
                }
                if (creature->m_entityData.currentAbility == Ability47) {
                    creature->m_attackPower = 0;
                    creature->m_entityData.currentAbility = AbilitySmash;
                }
            }
            if (!blocked) {
                plasma::Matrix<float> turning;
                turning.identity();
                turning.rotateZ(creature->m_entityData.rotation[2]);
                step += turning.transformDirection(plasma::Vector<3, float>(0.0f, speed * 2.0f, 0.0f));
            }
        }
        // Eating ends sitting; until then the eater stands still.
        if (creature->m_entityData.currentAbility == AbilityEat) {
            if (creature->m_entityData.timeSinceAbility > creature->m_entityData.actionItem.cooldown())
                creature->m_entityData.currentAbility = AbilitySit;
            else
                speed = 0.0f;
        }
        // Rolling interrupts the ability (a ninja keeps a few attacks).
        if (creature->m_entityData.rollTime != 0 &&
            !((creature->m_entityData.currentAbility == AbilityRetreat ||
               creature->m_entityData.currentAbility == AbilityShuriken) &&
              creature->m_entityData.timeSinceAbility <= creature->attackDuration())) {
            if (creature->m_entityData.classType == ClassRogue &&
                creature->m_entityData.specialization == SpecializationNinja) {
                int rollingAbility = creature->m_entityData.currentAbility;
                if (rollingAbility != AbilityPerforate && rollingAbility != AbilityKick &&
                    rollingAbility != AbilityAmbush)
                    creature->m_entityData.currentAbility = AbilityNone;
            } else {
                creature->m_entityData.currentAbility = AbilityNone;
                creature->m_entityData.charge = 0.0f;
            }
        }
        // Abilities that drive the movement themselves.
        bool driven = false;
        int movementPhaseAbility = creature->m_entityData.currentAbility;
        int &time = creature->m_entityData.timeSinceAbility;
        float &turn = creature->m_entityData.rotation[2];
        if ((movementPhaseAbility == AbilityHeadbutt || movementPhaseAbility == Ability93 ||
             movementPhaseAbility == AbilityHornCharge) &&
            time <= creature->anticipationTime())
            speed = 0.0f;
        if ((movementPhaseAbility == AbilityHeadbutt || movementPhaseAbility == Ability93 ||
             movementPhaseAbility == AbilityHornCharge) &&
            time > creature->anticipationTime() &&
            time < creature->anticipationTime() +
                       creature->strikeTime() * ForwardLeapStrikeWindowNumerator / ForwardLeapStrikeWindowDenominator) {
            // A forward leap.
            plasma::Matrix<float> turning;
            turning.identity();
            turning.rotateZ(turn);
            step = 20.0f * turning.transformDirection(plasma::Vector<3, float>(0.0f, 1.0f, 0.0f));
            speed = 50.0f;
            driven = true;
        }
        if (creature->m_entityData.currentAbility == AbilityDashLeft && time > creature->anticipationTime() &&
            time < creature->anticipationTime() + creature->strikeTime()) {
            // A dodge to the left.
            plasma::Matrix<float> turning;
            turning.identity();
            turning.rotateZ(turn);
            step = 20.0f * turning.transformDirection(plasma::Vector<3, float>(-1.0f, 0.0f, 0.0f));
            speed = 60.0f;
            driven = true;
        }
        if (creature->m_entityData.currentAbility == AbilityDashRight && time > creature->anticipationTime() &&
            time < creature->anticipationTime() + creature->strikeTime()) {
            // To the right.
            plasma::Matrix<float> turning;
            turning.identity();
            turning.rotateZ(turn);
            step = 20.0f * turning.transformDirection(plasma::Vector<3, float>(1.0f, 0.0f, 0.0f));
            speed = 60.0f;
            driven = true;
        }
        if (creature->m_entityData.currentAbility == AbilityDashForward && time > creature->anticipationTime() &&
            time < creature->anticipationTime() + creature->strikeTime()) {
            // Forwards.
            plasma::Matrix<float> turning;
            turning.identity();
            turning.rotateZ(turn);
            step = 20.0f * turning.transformDirection(plasma::Vector<3, float>(0.0f, 1.0f, 0.0f));
            speed = 60.0f;
            driven = true;
        }
        if (creature->m_entityData.currentAbility == AbilityShellBlock && time < creature->attackDuration()) {
            speed = 0.0f;
            driven = true;
        }
        if (creature->m_entityData.currentAbility == AbilityShellSpin && time < creature->attackDuration()) {
            // A spin: two turns a second.
            turn += seconds * 720.0f;
            driven = true;
            speed = 12.0f;
        }
        movementPhaseAbility = creature->m_entityData.currentAbility;
        if ((movementPhaseAbility == AbilityHeadbutt || movementPhaseAbility == Ability93 ||
             movementPhaseAbility == AbilityHornCharge) &&
            time > creature->anticipationTime() && time < creature->attackDuration())
            driven = true;
        if (creature->m_entityData.hostilityType == HostilityType4)
            speed = 30.0f;
        movementPhaseAbility = creature->m_entityData.currentAbility;
        if ((movementPhaseAbility == AbilityTwoHandedSweep || movementPhaseAbility == AbilityStomp) &&
            time < creature->attackDuration() && time >= creature->anticipationTime() &&
            time >= creature->anticipationTime()) {
            speed = 0.1f;
            driven = true;
        }
        // Charging builds the charge (to at most the mana): with the tempo, a sniper's with its combo,
        // buff 2 with skill 102; buff 10 fills it.
        float &charge = creature->m_entityData.charge;
        if (creature->m_entityData.currentAbility == AbilityChargeFireball)
            charge += creature->tempo() * (elapsed * 0.0005f);
        movementPhaseAbility = creature->m_entityData.currentAbility;
        if ((movementPhaseAbility == AbilityChargedShot || movementPhaseAbility == AbilitySalvo ||
             movementPhaseAbility == AbilityChargeBoomerang || movementPhaseAbility == AbilityBlock ||
             movementPhaseAbility == AbilityCrush || movementPhaseAbility == AbilitySwirl ||
             movementPhaseAbility == AbilityMonsterCharge) &&
            time >= creature->anticipationTime()) {
            float gain = creature->tempo() * (elapsed * 0.00075f);
            if (creature->m_entityData.classType == ClassRanger &&
                creature->m_entityData.specialization == SpecializationSniper)
                gain *= std::min(4.0f, (float)creature->m_entityData.hitCombo / (float)creature->maxCombo()) + 1.0f;
            if (creature->findBuff(BuffWarFrenzy))
                charge += (creature->abilitySkillBonus(AbilityWarFrenzy, -1) * 9.0f + 1.0f) * gain;
            else
                charge += gain;
            if (creature->findBuff(BuffInstantShoot))
                charge = creature->m_entityData.mp;
        }
        if (charge > creature->m_entityData.mp)
            charge = creature->m_entityData.mp;
        if (creature->m_entityData.slowedTime != 0)
            speed *= 0.5f;
        if (creature->m_entityData.sprintTime != 0)
            speed *= 1.5f;
        // Moving against the aim is up to half as fast.
        if (creature->m_entityData.binaryToggles.faceAim && creature->m_entityData.rollTime == 0 &&
            !((creature->m_entityData.currentAbility == AbilityRetreat ||
               creature->m_entityData.currentAbility == AbilityShuriken) &&
              time <= RetreatBackpedalExemptionMs)) {
            plasma::Vector<3, float> &aim = creature->m_entityData.attackRotation;
            plasma::Vector<3, float> &moving = creature->m_entityData.velocity;
            if (0.0f > plasma::Vector<2, float>(aim[0], aim[1]).dot(plasma::Vector<2, float>(moving[0], moving[1]))) {
                plasma::Vector<2, float> facing(aim[0], aim[1]);
                plasma::Vector<2, float> going(moving[0], moving[1]);
                facing.normalize();
                going.normalize();
                speed *= facing.dot(going) * 0.5f + 1.0f;
            }
        }
        // The push accelerates; the horizontal speed is capped.
        plasma::Vector<3, float> &motion = creature->m_entityData.velocity;
        motion += step;
        if (plasma::Vector<2, float>(motion[0], motion[1]).squaredLength() > speed * speed) {
            plasma::Vector<2, float> capped(motion[0], motion[1]);
            capped.normalize();
            capped *= speed;
            motion[0] = capped[0];
            motion[1] = capped[1];
        }
        // Falling: climbers hold on, floating creatures and gliders fall at a tenth of the gravity (30
        // blocks per second squared); the height where the fall began is kept (m_fallStartHeight).
        float &fall = motion[2];
        if (toggles.climbing && !toggles.gliding && (creature->m_entityData.flags & (1 << CreatureFlagAtWall))) {
            blend(motion, plasma::Vector<3, float>(0.0f, 0.0f, 0.0f), milliseconds, 0.0025f);
            if (fall > -10.0f)
                creature->m_fallStartHeight = (float)creature->m_entityData.position[2];
        } else if (((creature->m_entityData.appearance.appearanceFlags & (1 << AppearanceFlagFlying)) &&
                    creature->m_entityData.stunTime <= 0) ||
                   creature->isGliding()) {
            fall -= seconds * 0.1f * 30.0f;
            creature->m_fallStartHeight = (float)creature->m_entityData.position[2];
        } else {
            if (fall >= 0.0f)
                creature->m_fallStartHeight = (float)creature->m_entityData.position[2];
            fall -= seconds * 30.0f;
        }
        // The local player's glider: water ends it; hitting a wall fast or landing tilted stuns.
        if (creature == m_localPlayer && creature->m_entityData.binaryToggles.gliding) {
            unsigned int state = creature->m_entityData.flags;
            if (state & (1 << CreatureFlagInWater)) {
                creature->m_entityData.setToggles((1 << CreatureToggleGliding), false);
            } else if (!(state & (1 << CreatureFlagOnGround))) {
                if ((state & (1 << CreatureFlagAtWall)) && motion.squaredLength() > 64.0f &&
                    -4.0f > creature->m_wallNormal.dot(motion)) {
                    creature->m_entityData.stunTime = creature->maxStunTime();
                    creature->m_entityData.setToggles((1 << CreatureToggleGliding), false);
                    ServerUpdates::SoundPacket sound;
                    sound.position = creature->m_entityData.position;
                    sound.pitch = 1.0f;
                    sound.sound = SoundSwordHit03;
                    outgoing->sounds.push_back(sound);
                }
                if (creature->m_entityData.binaryToggles.fastMovement && 0.0f > fall)
                    fall += seconds * 30.0f;
                motion += seconds * wind(creature->m_entityData.position, motion);
            } else if (fabsf(roll) > 40.0f) {
                creature->m_entityData.stunTime = creature->maxStunTime();
                creature->m_entityData.setToggles((1 << CreatureToggleGliding), false);
                ServerUpdates::SoundPacket sound;
                sound.position = creature->m_entityData.position;
                sound.pitch = 1.0f;
                sound.sound = SoundSwordHit03;
                outgoing->sounds.push_back(sound);
            }
        }
        if (creature->m_entityData.stunTime > 0) {
            creature->m_entityData.setToggles((1 << CreatureToggleGliding), false);
            creature->m_entityData.setToggles((1 << CreatureToggleClimbing), false);
            creature->m_entityData.charge = 0.0f;
        }
        if (creature->m_entityData.currentAbility != AbilityNone &&
            creature->m_entityData.timeSinceAbility < creature->attackDuration()) {
            creature->m_entityData.setToggles((1 << CreatureToggleGliding), false);
            creature->m_entityData.setToggles((1 << CreatureToggleClimbing), false);
        }
        plasma::Vector<3, float> &retreat = creature->m_entityData.retreat;
        step = seconds * (motion + retreat);

        // Creatures push each other apart (a player only enemies and objects), the smaller one the more.
        if (creature->m_entityData.hostilityType != HostilityAttackableObject &&
            creature->m_entityData.currentAbility != AbilitySit &&
            creature->m_entityData.currentAbility != AbilitySleep) {
            for (std::map<long long, Creature *>::iterator other = m_creatures.begin(); other != m_creatures.end();
                 other++) {
                Creature *&obstacle = other->second;
                if (!obstacle || obstacle == creature)
                    continue;
                if (creature->m_entityData.hostilityType == HostilityPlayer &&
                    obstacle->m_entityData.hostilityType != HostilityEnemy &&
                    obstacle->m_entityData.hostilityType != HostilityAttackableObject)
                    continue;
                if (0.0f >= obstacle->m_entityData.hp)
                    continue;
                plasma::Vector<3, float> extent = obstacle->m_entityData.appearance.size;
                if (obstacle->m_entityData.hostilityType != HostilityAttackableObject)
                    extent += plasma::Vector<3, float>(plasma::Vector<3, int>(1, 1, 0));
                plasma::Vector<3, Fixed> &position = creature->m_entityData.position;
                plasma::Vector<3, Fixed> &theirs = obstacle->m_entityData.position;
                // Half-open boxes, as for the zone objects.
                if (!(position[0] + width * 0.5f >= theirs[0] - extent[0] * 0.5f) ||
                    !(position[0] - width * 0.5f < theirs[0] + extent[0] * 0.5f) ||
                    !(position[1] + size[1] * 0.5f >= theirs[1] - extent[1] * 0.5f) ||
                    !(position[1] - size[1] * 0.5f < theirs[1] + extent[1] * 0.5f) ||
                    !(position[2] + size[2] * 0.5f >= theirs[2] - extent[2] * 0.5f) ||
                    !(position[2] - size[2] * 0.5f < theirs[2] + extent[2] * 0.5f))
                    continue;
                plasma::Vector<3, float> away(position - theirs);
                away[2] = 0.0f;
                if (!(away.squaredLength() > 0.0f))
                    continue;
                away.normalize();
                if (creature->m_entityData.stunTime > 0)
                    away *= 0.1f;
                step += seconds * 5.0f * obstacle->m_entityData.appearance.size[0] / width * away;
            }
        }
        // The step is taken in parts of at most a block.
        int parts = (int)(step.length() + 1.0f);
        step *= 1.0f / (float)parts;
        plasma::Vector<3, Fixed> start = creature->m_entityData.position;
        bool landed = false;
        bool hitWall = false;
        bool onShip = false;
        if ((!m_client || creature == m_localPlayer) &&
            creature->m_entityData.hostilityType != HostilityAttackableObject)
            resolveCollisions(creature);
        // Each part moves along the axes in turn against the blocks, the zone objects and the airships.
        plasma::Vector<3, Fixed> &position = creature->m_entityData.position;
        for (int part = 0; part < parts; part++) {
            Fixed shipBase(0);
            for (int axis = 0; axis < 3; axis++) {
                float &move = step[axis];
                Fixed &coordinate = position[axis];
                coordinate += Fixed(move);
                bool colliding = false;
                bool respond = false;
                plasma::Vector<3, int> low = toBlock(position - plasma::Vector<3, Fixed>(0.5f * size));
                plasma::Vector<3, int> high = toBlock(position + plasma::Vector<3, Fixed>(0.5f * size));
                // A swimmer (AppearanceFlagFish) collides with all but water.
                for (int x = low[0]; x <= high[0] && !colliding; x++) {
                    for (int y = low[1]; y <= high[1] && !colliding; y++) {
                        for (int z = low[2]; z <= high[2]; z++) {
                            if (getBlock(x, y, z, 0).isSolid() ||
                                ((creature->m_entityData.appearance.appearanceFlags & (1 << AppearanceFlagFish)) &&
                                 getBlock(x, y, z, 0).blockType() != BlockWater))
                                colliding = true;
                        }
                    }
                }
                if (colliding && (axis != 2 || landed))
                    respond = true;
                if (!respond) {
                    // The objects of the 3 x 3 column cells: open doors and windows, traps, levers and the objects it
                    // is leaving let it pass, as does moving away from one.
                    int cellX = (int)position[0] / BlocksPerObjectCell;
                    int cellY = (int)position[1] / BlocksPerObjectCell;
                    for (int x = cellX - 1; x <= cellX + 1 && !respond; x++) {
                        for (int y = cellY - 1; y <= cellY + 1 && !respond; y++) {
                            std::list<Zone::CellRecord> *objects = getCellObjects(x, y, 0);
                            if (objects) {
                                for (std::list<Zone::CellRecord>::iterator it2 = objects->begin();
                                     it2 != objects->end(); it2++) {
                                    Zone::CellRecord &cell = *it2;
                                    int type = cell.object->type;
                                    if (type == StaticObjectSpikeTrap || type == StaticObjectFireTrap ||
                                        type == StaticObjectLever)
                                        continue;
                                    if (creature->m_overlappedObjects.find(cell.object) !=
                                        creature->m_overlappedObjects.end())
                                        continue;
                                    type = cell.object->type;
                                    // These pass while inactive.
                                    if ((type == StaticObjectDoor || type == StaticObjectStompTrap ||
                                         type == StaticObjectBigDoor || type == StaticObjectWindow ||
                                         type == StaticObjectGate) &&
                                        !cell.object->active)
                                        continue;
                                    plasma::Vector<3, float> extent = cell.object->size;
                                    if (cell.object->rotation % 2 != 0)
                                        std::swap(extent[0], extent[1]);
                                    if ((coordinate - cell.object->position[axis]) * move >= 0.0f)
                                        continue;
                                    // Overlap of half-open boxes: the far sides are compared strictly.
                                    plasma::Vector<3, Fixed> &place = cell.object->position;
                                    if (!(position[0] + width * 0.5f >= place[0] - extent[0] * 0.5f) ||
                                        !(position[0] - width * 0.5f < place[0] + extent[0] * 0.5f) ||
                                        !(position[1] + size[1] * 0.5f >= place[1] - extent[1] * 0.5f) ||
                                        !(position[1] - size[1] * 0.5f < place[1] + extent[1] * 0.5f) ||
                                        !(position[2] + size[2] * 0.5f >= place[2]) ||
                                        !(position[2] - size[2] * 0.5f < place[2] + extent[2]))
                                        continue;
                                    if (axis == 2 && 0.0f > fall)
                                        landed = true;
                                    hitWall = true;
                                    canStepUp = false;
                                    respond = true;
                                    break;
                                }
                            }
                            if (colliding)
                                respond = true;
                        }
                    }
                }
                if (!respond) {
                    // The airships within 64 blocks: their model's voxels (turned and placed like the ship) are solid;
                    // touching one makes the creature a rider.
                    for (std::map<long long, Zone::Airship *>::iterator it2 = m_airships.begin();
                         it2 != m_airships.end(); it2++) {
                        Zone::Airship *&ship = it2->second;
                        if (plasma::Vector<3, float>(position - ship->currentPosition).squaredLength() > 4096.0f ||
                            !ship)
                            continue;
                        Sprite *model = m_spriteManager.sprite(SpriteAirship);
                        if (!model)
                            continue;
                        plasma::Matrix<float> local;
                        local.identity();
                        local.translate((float)model->sizeX() * 0.5f, (float)model->sizeY() * 0.5f,
                                        (float)model->sizeZ() * 0.5f);
                        local.rotateZ(-ship->currentRotation);
                        local.translate(plasma::Vector<3, float>(position - ship->currentPosition));
                        plasma::Vector<3, int> first = toBlock(
                            plasma::Vector<3, Fixed>(local * plasma::Vector<3, float>(0.0f, 0.0f, 0.0f) - 0.5f * size));
                        plasma::Vector<3, int> last = toBlock(
                            plasma::Vector<3, Fixed>(local * plasma::Vector<3, float>(0.0f, 0.0f, 0.0f) + 0.5f * size));
                        for (int x = first[0]; x <= last[0]; x++) {
                            for (int y = first[1]; y <= last[1]; y++) {
                                for (int z = first[2]; z <= last[2]; z++) {
                                    if (!(model->voxel(x, y, z) != plasma::Vector<3, unsigned char>(0, 0, 0)))
                                        continue;
                                    if (axis == 2)
                                        onShip = true;
                                    colliding = true;
                                    shipBase = ship->currentPosition[2] - (float)model->sizeZ() * 0.5f;
                                    ship->riders.insert(creature->m_id);
                                }
                            }
                        }
                    }
                    if (!colliding)
                        continue;
                }
                if (axis == 2) {
                    // Landing: a thud from 2 blocks per second; the local player falling more than 20 blocks at more
                    // than 5 blocks per second takes half its hp per 20 blocks beyond and is stunned.
                    if (m_localPlayer && -2.0f > fall) {
                        ServerUpdates::SoundPacket sound;
                        sound.sound = SoundStep2;
                        sound.pitch = (float)rand() * 0.2f / float(RAND_MAX) + 1.0f;
                        sound.position = position;
                        sound.volume = 0.5f;
                        outgoing->sounds.push_back(sound);
                    }
                    Fixed &height = position[2];
                    if (!(creature->m_entityData.flags & (1 << CreatureFlagInWater)) && -5.0f > fall &&
                        creature->m_fallStartHeight - height > 20.0f && creature == m_localPlayer) {
                        float damage =
                            (float)(((creature->m_fallStartHeight - height) / 20.0f - 1) * creature->maxHP() * 0.5f);
                        creature->m_entityData.hp -= damage;
                        int stun = creature->m_entityData.stunTime;
                        if (!(stun > creature->maxStunTime()))
                            stun = creature->maxStunTime();
                        creature->m_entityData.stunTime = stun;
                        creature->m_entityData.setToggles((1 << CreatureToggleClimbing), false);
                        ServerUpdates::SoundPacket sound;
                        sound.position = creature->m_entityData.position;
                        sound.pitch = 1.0f;
                        sound.sound = SoundSwordHit03;
                        outgoing->sounds.push_back(sound);
                        if (0.0f > creature->m_entityData.hp)
                            creature->m_entityData.hp = 0.0f;
                        HitPacket hit;
                        hit.targetId = creature->m_id;
                        hit.attackerId = 0;
                        hit.damage = damage;
                        hit.position = creature->m_entityData.position;
                        hit.critical = 0;
                        outgoing->hits.push_back(hit);
                    }
                    height -= Fixed(move);
                    fall = 0.0f;
                    retreat[2] = 0.0f;
                    creature->m_fallStartHeight = (float)height;
                    continue;
                }
                if (!canStepUp || (creature->m_entityData.appearance.appearanceFlags & (1 << AppearanceFlagFish))) {
                    coordinate -= Fixed(move);
                    continue;
                }
                // A step up: the box one block higher must be free of blocks and airship voxels.
                bool raisedBlocked = false;
                low = toBlock(position + plasma::Vector<3, Fixed>(plasma::Vector<3, float>(0.0f, 0.0f, 1.01f)) -
                              plasma::Vector<3, Fixed>(0.5f * size));
                high = toBlock(position + plasma::Vector<3, Fixed>(plasma::Vector<3, float>(0.0f, 0.0f, 1.01f)) +
                               plasma::Vector<3, Fixed>(0.5f * size));
                for (int x = low[0]; x <= high[0] && !raisedBlocked; x++) {
                    for (int y = low[1]; y <= high[1] && !raisedBlocked; y++) {
                        for (int z = low[2]; z <= high[2]; z++) {
                            if (getBlock(x, y, z, 0).isSolid()) {
                                raisedBlocked = true;
                                break;
                            }
                        }
                    }
                }
                for (std::map<long long, Zone::Airship *>::iterator it2 = m_airships.begin();
                     !raisedBlocked && it2 != m_airships.end(); it2++) {
                    Zone::Airship *&ship = it2->second;
                    if (plasma::Vector<3, float>(position - ship->currentPosition).squaredLength() > 4096.0f || !ship)
                        continue;
                    Sprite *model = m_spriteManager.sprite(SpriteAirship);
                    if (!model)
                        continue;
                    plasma::Matrix<float> local;
                    local.identity();
                    local.translate((float)model->sizeX() * 0.5f, (float)model->sizeY() * 0.5f,
                                    (float)model->sizeZ() * 0.5f);
                    local.rotateZ(-ship->currentRotation);
                    local.translate(plasma::Vector<3, float>(position - ship->currentPosition));
                    plasma::Vector<3, int> first = toBlock(
                        plasma::Vector<3, Fixed>(local * plasma::Vector<3, float>(0.0f, 0.0f, 1.01f) - 0.5f * size));
                    plasma::Vector<3, int> last = toBlock(
                        plasma::Vector<3, Fixed>(local * plasma::Vector<3, float>(0.0f, 0.0f, 1.01f) + 0.5f * size));
                    for (int x = first[0]; x <= last[0] && !raisedBlocked; x++) {
                        for (int y = first[1]; y <= last[1] && !raisedBlocked; y++) {
                            for (int z = first[2]; z <= last[2]; z++) {
                                if (model->voxel(x, y, z) != plasma::Vector<3, unsigned char>(0, 0, 0)) {
                                    ship->riders.insert(creature->m_id);
                                    raisedBlocked = true;
                                    break;
                                }
                            }
                        }
                    }
                }
                if (raisedBlocked) {
                    coordinate -= Fixed(move);
                    continue;
                }
                // Up onto the next block, drawn smoothly (m_stepOffset).
                Fixed &height = position[2];
                height -= shipBase;
                Fixed before = height;
                float half = size[2] * 0.5f;
                height = Fixed(toBlock(before - half)) + half + 1.01;
                creature->m_stepOffset = (float)(height - (before - creature->m_stepOffset));
                height += shipBase;
            }
        }
        // The new state flags.
        unsigned int before = creature->m_entityData.flags;
        bool wasInWater = (before >> CreatureFlagInWater) & 1;
        bool wasOnGround = before & (1 << CreatureFlagOnGround);
        bool wasAtWall = (before >> CreatureFlagAtWall) & 1;
        plasma::Vector<3, float> wallBefore = creature->m_wallNormal;
        creature->m_entityData.flags = 0;
        creature->m_pathSearchTime += milliseconds;
        // Stuck (CreatureFlagMovementBlocked): pushing and moving fast but going nowhere.
        if (creature->m_entityData.stunTime <= 0 &&
            plasma::Vector<2, float>(motion[0], motion[1]).squaredLength() > 16.0f &&
            creature->m_entityData.acceleration.squaredLength() > 16.0f &&
            step.squaredLength() * 0.01f > plasma::Vector<3, float>(start - position).squaredLength())
            creature->m_entityData.flags |= (1 << CreatureFlagMovementBlocked);
        if (hitWall)
            creature->m_entityData.flags |= (1 << CreatureFlagHitObject);
        plasma::Vector<3, int> low = toBlock(position - plasma::Vector<3, Fixed>(0.5f * size));
        plasma::Vector<3, int> high = toBlock(position + plasma::Vector<3, Fixed>(0.5f * size));
        // CreatureFlagPartlyAboveWater: not all of the box is in water.
        for (int x = low[0]; x <= high[0] && !(creature->m_entityData.flags & (1 << CreatureFlagPartlyAboveWater));
             x++) {
            for (int y = low[1]; y <= high[1] && !(creature->m_entityData.flags & (1 << CreatureFlagPartlyAboveWater));
                 y++) {
                for (int z = low[2]; z <= high[2]; z++) {
                    if (getBlock(x, y, z, 0).blockType() != BlockWater) {
                        creature->m_entityData.flags |= (1 << CreatureFlagPartlyAboveWater);
                        break;
                    }
                }
            }
        }
        // In water the creature floats up by how deep it is (ability 107 more so).
        bool wet = false;
        if (creature->m_entityData.hostilityType != HostilityAttackableObject) {
            int surface = low[2];
            for (int x = low[0]; x <= high[0]; x++) {
                for (int y = low[1]; y <= high[1]; y++) {
                    for (int z = low[2]; z <= high[2]; z++) {
                        if (getBlock(x, y, z, 0).blockType() == BlockWater) {
                            if (z + 1 > surface)
                                surface = z + 1;
                            wet = true;
                            creature->m_entityData.flags |= (1 << CreatureFlagInWater);
                        }
                    }
                }
            }
            if (creature->m_entityData.flags & (1 << CreatureFlagInWater)) {
                float depth = (float)(Fixed(surface) - position[2]) / (size[2] * 0.5f);
                float lift = 0.0f;
                if (depth > 0.0f) {
                    if (1.0f > depth)
                        lift = depth + 1.0f;
                    else
                        lift = 1.0f;
                    if (creature->m_entityData.currentAbility == AbilitySailBoat)
                        lift = depth + 1.0f;
                }
                fall += seconds * 30.0f * lift;
            }
        }
        // Friction: slower while pushing.
        float friction = 0.005f;
        if (creature->m_entityData.acceleration.squaredLength() > 0.0f)
            friction = 0.0025f;
        // On the ground: a solid block 0.2 below the feet (block type 3 there counts as wet).
        if (!(creature->m_entityData.flags & (1 << CreatureFlagInWater))) {
            int footZ = toBlock(position[2] - size[2] * 0.5f - 0.2f);
            for (int x = low[0]; x <= high[0] && !(creature->m_entityData.flags & (1 << CreatureFlagOnGround)); x++) {
                for (int y = low[1]; y <= high[1]; y++) {
                    if (getBlock(x, y, footZ, 0).isSolid())
                        creature->m_entityData.flags |= (1 << CreatureFlagOnGround);
                    if (getBlock(x, y, footZ, 0).blockType() == BlockWet)
                        wet = true;
                }
            }
        }
        // A pet in water restores its owner's riding stamina.
        if (wet && creature->m_entityData.hostilityType == HostilityPet) {
            Creature *owner = creatureByID(creature->m_entityData.ownerId);
            if (owner && 1.0f > owner->m_ridingStamina) {
                owner->m_ridingStamina += seconds * 0.05f;
                if (owner->m_ridingStamina > 1.0f)
                    owner->m_ridingStamina = 1.0f;
            }
        }
        // Walking off a ledge of at most a block keeps the creature on the ground, drawn smoothly down.
        if (wasOnGround && canStepUp && plasma::Vector<2, float>(step[0], step[1]).squaredLength() > 0.0f &&
            !(creature->m_entityData.flags & ((1 << CreatureFlagOnGround) | (1 << CreatureFlagInWater))) &&
            0.0f >= step[2]) {
            Fixed &height = position[2];
            float &tall = size[2];
            int below = toBlock(height - tall * 0.5f - 1.2f);
            for (int x = low[0]; x <= high[0] && !(creature->m_entityData.flags & (1 << CreatureFlagOnGround)); x++) {
                for (int y = low[1]; y <= high[1]; y++) {
                    if (!getBlock(x, y, below, 0).isSolid())
                        continue;
                    creature->m_entityData.flags |= (1 << CreatureFlagOnGround);
                    Fixed drawn = height - creature->m_stepOffset;
                    height = Fixed(below) + 1.01f + tall * 0.5f;
                    creature->m_stepOffset = (float)(height - drawn);
                    break;
                }
            }
        }
        if (landed || onShip)
            creature->m_entityData.flags |= (1 << CreatureFlagOnGround);
        // Swimming (ability 107) brakes slowly; the ground brakes, except charges, dives and the first
        // part of some attacks.
        if (creature->m_entityData.currentAbility == AbilitySailBoat &&
            (creature->m_entityData.flags & (1 << CreatureFlagInWater))) {
            float drag = friction * 0.1f;
            float zero = 0.0f;
            blend(motion[0], zero, milliseconds, drag);
            float none = 0.0f;
            blend(motion[1], none, milliseconds, drag);
            float still = 0.0f;
            blend(fall, still, milliseconds, friction);
        }
        int brakingAbility = creature->m_entityData.currentAbility;
        if ((creature->m_entityData.flags & (1 << CreatureFlagOnGround)) && brakingAbility != AbilityIntercept &&
            !(brakingAbility == AbilitySmash && time <= creature->anticipationTime() + creature->strikeTime()) &&
            !((brakingAbility == AbilityRightStab || brakingAbility == AbilityLeftStab ||
               brakingAbility == AbilityKick || brakingAbility == AbilityRightDaggerPierce ||
               brakingAbility == AbilityLeftDaggerPierce || brakingAbility == AbilityAmbush ||
               brakingAbility == AbilityBlockJab) &&
              time < creature->anticipationTime()))
            blend(motion, plasma::Vector<3, float>(0.0f, 0.0f, 0.0f), milliseconds, friction);
        if ((creature->m_entityData.flags & ((1 << CreatureFlagOnGround) | (1 << CreatureFlagInWater))) ||
            creature->m_entityData.binaryToggles.gliding || !creature->m_path.empty())
            blend(retreat, plasma::Vector<3, float>(0.0f, 0.0f, 0.0f), milliseconds, friction);

        // Walls: a solid block just beside the box on each side (0.2 blocks out for players), else at the
        // corners away from the facing.
        plasma::Matrix<float> facing;
        facing.identity();
        facing.rotateZ(turn);
        plasma::Vector<3, float> forward = facing.transformDirection(plasma::Vector<3, float>(0.0f, 1.0f, 0.0f));
        creature->m_wallNormal = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
        bool player = creature->m_entityData.hostilityType == HostilityPlayer;
        Fixed &px = position[0];
        Fixed &py = position[1];
        float halfX = width * 0.5f;
        float halfY = size[1] * 0.5f;
        int minusX = player ? (int)(px - halfX - 0.2f) : (int)(px - (int)(halfX + 0.5f) - 0.5f - 0.05f);
        int plusX = player ? (int)(px + halfX + 0.2f) : (int)(px + (int)(halfX + 0.5f) + 0.5f + 0.05f);
        int minusY = player ? (int)(py - halfY - 0.2f) : (int)(py - (int)(halfY + 0.5f) - 0.5f - 0.05f);
        int plusY = player ? (int)(py + halfY + 0.2f) : (int)(py + (int)(halfY + 0.5f) + 0.5f + 0.05f);
        // Each side is tested on its own : a creature in a corner touches two walls and gets both normals.
        bool wallMinusY = false;
        for (int x = low[0]; x <= high[0] && !wallMinusY; x++) {
            for (int z = low[2]; z <= high[2]; z++) {
                if (getBlock(x, minusY, z, 0).isSolid()) {
                    creature->m_entityData.flags |= (1 << CreatureFlagAtWall);
                    creature->m_wallNormal[1] = 1.0f;
                    wallMinusY = true;
                    break;
                }
            }
        }
        bool wallPlusY = false;
        for (int x = low[0]; x <= high[0] && !wallPlusY; x++) {
            for (int z = low[2]; z <= high[2]; z++) {
                if (getBlock(x, plusY, z, 0).isSolid()) {
                    creature->m_entityData.flags |= (1 << CreatureFlagAtWall);
                    creature->m_wallNormal[1] = -1.0f;
                    wallPlusY = true;
                    break;
                }
            }
        }
        bool wallMinusX = false;
        for (int y = low[1]; y <= high[1] && !wallMinusX; y++) {
            for (int z = low[2]; z <= high[2]; z++) {
                if (getBlock(minusX, y, z, 0).isSolid()) {
                    creature->m_entityData.flags |= (1 << CreatureFlagAtWall);
                    creature->m_wallNormal[0] = 1.0f;
                    wallMinusX = true;
                    break;
                }
            }
        }
        bool wallPlusX = false;
        for (int y = low[1]; y <= high[1] && !wallPlusX; y++) {
            for (int z = low[2]; z <= high[2]; z++) {
                if (getBlock(plusX, y, z, 0).isSolid()) {
                    creature->m_entityData.flags |= (1 << CreatureFlagAtWall);
                    creature->m_wallNormal[0] = -1.0f;
                    wallPlusX = true;
                    break;
                }
            }
        }
        if (!(creature->m_entityData.flags & (1 << CreatureFlagAtWall)) &&
            0.1f >= forward.dot(plasma::Vector<3, float>(0.7f, 0.7f, 0.0f))) {
            for (int z = low[2]; z <= high[2]; z++) {
                if (getBlock(minusX, minusY, z, 0).isSolid()) {
                    creature->m_entityData.flags |= (1 << CreatureFlagAtWall);
                    creature->m_wallNormal[0] = 1.0f;
                    creature->m_wallNormal[1] = 1.0f;
                    break;
                }
            }
        }
        if (!(creature->m_entityData.flags & (1 << CreatureFlagAtWall)) &&
            0.1f >= forward.dot(plasma::Vector<3, float>(-0.7f, 0.7f, 0.0f))) {
            for (int z = low[2]; z <= high[2]; z++) {
                if (getBlock(plusX, minusY, z, 0).isSolid()) {
                    creature->m_entityData.flags |= (1 << CreatureFlagAtWall);
                    creature->m_wallNormal[0] = -1.0f;
                    creature->m_wallNormal[1] = 1.0f;
                    break;
                }
            }
        }
        if (!(creature->m_entityData.flags & (1 << CreatureFlagAtWall)) &&
            0.1f >= forward.dot(plasma::Vector<3, float>(0.7f, -0.7f, 0.0f))) {
            for (int z = low[2]; z <= high[2]; z++) {
                if (getBlock(minusX, plusY, z, 0).isSolid()) {
                    creature->m_entityData.flags |= (1 << CreatureFlagAtWall);
                    creature->m_wallNormal[0] = 1.0f;
                    creature->m_wallNormal[1] = -1.0f;
                    break;
                }
            }
        }
        if (!(creature->m_entityData.flags & (1 << CreatureFlagAtWall)) &&
            0.1f >= forward.dot(plasma::Vector<3, float>(-0.7f, -0.7f, 0.0f))) {
            for (int z = low[2]; z <= high[2]; z++) {
                if (getBlock(plusX, plusY, z, 0).isSolid()) {
                    creature->m_entityData.flags |= (1 << CreatureFlagAtWall);
                    creature->m_wallNormal[0] = -1.0f;
                    creature->m_wallNormal[1] = -1.0f;
                    break;
                }
            }
        }
        // In water (not swimming with ability 107) the creature slows and, unless it is an object or
        // AppearanceFlagQuadruped holds it, tilts forward; diving in fast splashes.
        float &pitch = creature->m_entityData.rotation[0];
        if ((creature->m_entityData.flags & (1 << CreatureFlagInWater)) &&
            creature->m_entityData.currentAbility != AbilitySailBoat) {
            blend(motion, plasma::Vector<3, float>(0.0f, 0.0f, 0.0f), milliseconds, 0.005f);
            if (creature->m_entityData.hostilityType != HostilityAttackableObject &&
                !(creature->m_entityData.appearance.appearanceFlags & (1 << AppearanceFlagQuadruped))) {
                float tilt = -60.0f;
                blend(pitch, tilt, milliseconds, 0.01f);
            }
            if (!wasInWater && -3.0f > fall) {
                ServerUpdates::ParticlePacket splash;
                splash.position = position;
                // Water-entry splash RGBA palette.
                splash.size = 0.4f;
                splash.color = plasma::Vector<4, float>(0.2f, 0.7f, 1.0f, 1.0f);
                splash.acceleration = plasma::Vector<3, float>(0.0f, 0.0f, 10.0f);
                splash.count = WaterSplashParticleCount;
                outgoing->particles.push_back(splash);
                ServerUpdates::SoundPacket sound;
                sound.sound = SoundWaterSplash01;
                sound.pitch = (float)rand() / float(RAND_MAX) + 0.9f;
                sound.position = position;
                sound.volume = 1.0f;
                outgoing->sounds.push_back(sound);
            }
        } else {
            float level = 0.0f;
            blend(pitch, level, milliseconds, 0.01f);
        }
        // The local player climbing over the top of a wall it faces is pushed onto it.
        if (creature == m_localPlayer && 0.0f >= forward.dot(wallBefore) && toggles.climbing && !toggles.gliding &&
            wasAtWall && !(creature->m_entityData.flags & (1 << CreatureFlagAtWall)) &&
            plasma::Vector<2, float>(creature->m_entityData.acceleration[0], creature->m_entityData.acceleration[1])
                    .squaredLength() > 0.0f)
            motion = -5.0f * wallBefore;
        // Race 101 rears up during the first second of ability 51.
        if (creature->m_entityData.race == RaceDragon) {
            float rear = creature->m_entityData.currentAbility == AbilityForwardRoll && time < 1000 ? 60.0f : 0.0f;
            blend(pitch, rear, milliseconds, 0.01f);
        }
        // Riding a living pet: on top of it, leaning forward, moving with its walk cycle.
        long long &petId = creature->m_petId;
        Creature *mount = creatureByID(petId);
        bool riding = false;
        if (mount) {
            if (0.0f >= mount->m_entityData.hp) {
                petId = 0;
            } else if (creature->m_entityData.hostilityType != HostilityPet) {
                creature->m_entityData.position =
                    mount->m_entityData.position +
                    plasma::Vector<3, Fixed>(plasma::Vector<3, float>(0.0f, 0.0f, 1.5f - mount->m_stepOffset));
                motion = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
                creature->m_entityData.rotation =
                    mount->m_entityData.rotation + plasma::Vector<3, float>(20.0f, 0.0f, 0.0f);
                creature->m_entityData.acceleration = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
                creature->m_walkAmplitude = mount->m_walkAmplitude * 0.5f;
                creature->m_walkPhase = mount->m_walkPhase;
                riding = true;
            }
        }
        // The walk cycle: its phase advances with the speed (a footstep sound every half turn for the
        // local player), its weight grows while moving and resets in the air.
        float stride = sqrtf(0.8f / width);
        if (!riding) {
            unsigned int now = creature->m_entityData.flags;
            bool grounded = (now & (1 << CreatureFlagOnGround)) ||
                            (creature->m_entityData.appearance.appearanceFlags & (1 << AppearanceFlagFlying)) ||
                            (toggles.climbing && (now & (1 << CreatureFlagAtWall))) ||
                            (now & (1 << CreatureFlagInWater));
            if (grounded && motion.squaredLength() > 0.5f && creature->m_entityData.currentAbility != AbilitySailBoat) {
                if (toggles.climbing && (creature->m_entityData.flags & (1 << CreatureFlagAtWall))) {
                    creature->m_walkPhase += stride * (motion.length() * seconds * 6.0f);
                    creature->m_walkAmplitude += seconds * 0.2f;
                } else {
                    float phase = creature->m_walkPhase;
                    float pace = plasma::Vector<2, float>(motion[0], motion[1]).length();
                    if (creature->m_entityData.flags & (1 << CreatureFlagOnGround)) {
                        int movingAbility = creature->m_entityData.currentAbility;
                        if (movingAbility == AbilityIntercept ||
                            (movingAbility == AbilitySmash && time < creature->attackDuration()) ||
                            ((movingAbility == AbilityRightStab || movingAbility == AbilityLeftStab ||
                              movingAbility == AbilityKick || movingAbility == AbilityRightDaggerPierce ||
                              movingAbility == AbilityLeftDaggerPierce || movingAbility == AbilityAmbush ||
                              movingAbility == AbilityBlockJab) &&
                             time < creature->anticipationTime()))
                            creature->m_walkPhase += stride * (elapsed * (0.001f * 0.1f) * pace * 1.5f);
                        else if (creature->m_entityData.currentAbility == AbilitySneak)
                            creature->m_walkPhase += stride * (pace * seconds * 1.5f) * 2.0f;
                        else
                            creature->m_walkPhase += stride * (pace * seconds * 1.5f);
                        creature->m_walkAmplitude += seconds;
                        if (m_localPlayer && creature->m_entityData.rollTime == 0 &&
                            !(creature->m_entityData.flags & (1 << CreatureFlagInWater)) && pace > 5.0f &&
                            (int)(phase / 3.141592653589793) < (int)(creature->m_walkPhase / 3.141592653589793)) {
                            ServerUpdates::SoundPacket sound;
                            Fixed &x = position[0];
                            Fixed &y = position[1];
                            if (getBlock(x, y, position[2] - size[2] * 0.5f - 0.5f, 0).blockType() == BlockWet) {
                                sound.sound = SoundStepWater + rand() % (SoundStepWater3 - SoundStepWater + 1);
                                sound.pitch = 1.0f;
                            } else {
                                sound.sound = SoundStep2;
                                sound.pitch = (float)rand() * 0.2f / float(RAND_MAX) + 1.0f;
                            }
                            sound.position = position;
                            sound.volume = 1.0f;
                            outgoing->sounds.push_back(sound);
                        }
                    } else {
                        creature->m_walkPhase += stride * (elapsed * 0.002f * pace * 1.5f);
                        creature->m_walkAmplitude += seconds;
                    }
                }
            } else if (!(now & (1 << CreatureFlagOnGround)) && !(now & (1 << CreatureFlagInWater)) &&
                       !(toggles.climbing && (now & (1 << CreatureFlagAtWall)))) {
                creature->m_walkPhase = 0.0f;
                creature->m_walkAmplitude += stride * seconds;
            }
        }
        // Gliding needs the hang glider (special item 23/0), the lamp a lamp.
        Item &special = creature->m_entityData.equipment.special;
        if (!(special.category == ItemCategorySpecial && special.id == SpecialItemHangGlider))
            creature->m_entityData.binaryToggles.gliding = 0;
        if (creature->m_entityData.equipment.lamp.category != ItemCategoryLamp)
            creature->m_entityData.binaryToggles.lampOn = 0;
        float &weight = creature->m_walkAmplitude;
        if (weight > 1.0f)
            weight = 1.0f;
        float rest = 0.0f;
        blend(weight, rest, milliseconds, 0.005f);
        float &lift = creature->m_stepOffset;
        if (0.0f > lift) {
            float ground = 0.0f;
            blend(lift, ground, milliseconds, 0.01f);
        } else {
            float ground = 0.0f;
            blend(lift, ground, milliseconds, 0.02f);
        }
        float unlit = 0.0f;
        blend(creature->m_hitFlash, unlit, milliseconds, 0.0075f);

        // Facing: a climber faces the wall and looks up; otherwise the head levels, and the body turns to
        // the aim (toggle faceAim) or to the way it walks.
        float &head = creature->m_entityData.headRotation;
        if (toggles.climbing && (creature->m_entityData.flags & (1 << CreatureFlagAtWall)) && !toggles.gliding) {
            float up = 45.0f;
            blend(head, up, milliseconds, 0.005f);
            plasma::Vector<3, float> into = -creature->m_wallNormal;
            into.normalize();
            float &along = into[1];
            if (along > 1.0f)
                along = 1.0f;
            if (-1.0f > along)
                along = -1.0f;
            float heading;
            if (into[0] > 0.0f)
                heading = (float)(-acosf(along) / 3.141592653589793 * 180.0);
            else
                heading = (float)(acosf(along) / 3.141592653589793 * 180.0);
            blend(turn, interpolateAngle(turn, heading, 1.0f), milliseconds, 0.005f);
        } else {
            head *= expf(elapsed * -0.01f);
            if (creature->m_entityData.binaryToggles.faceAim && creature->m_entityData.stunTime <= 0) {
                plasma::Vector<3, float> &aim = creature->m_drawnAim;
                plasma::Vector<2, float> flat(aim[0], aim[1]);
                if (flat.squaredLength() > 0.0f && !driven) {
                    flat.normalize();
                    float &along = flat[1];
                    if (along > 1.0f)
                        along = 1.0f;
                    if (-1.0f > along)
                        along = -1.0f;
                    float heading;
                    if (flat[0] > 0.0f)
                        heading = (float)(-acosf(along) / 3.141592653589793 * 180.0);
                    else
                        heading = (float)(acosf(along) / 3.141592653589793 * 180.0);
                    blend(turn, interpolateAngle(turn, heading, 1.0f), milliseconds, 0.1f);
                }
                plasma::Vector<3, float> look = aim;
                if (look.squaredLength() > 0.0f) {
                    look.normalize();
                    float tilt = (float)(asinf(look[2]) / 3.141592653589793 * 180.0);
                    blend(head, interpolateAngle(head, tilt, 1.0f), milliseconds, 0.005f);
                }
            } else if (creature->m_entityData.rollTime == 0 &&
                       plasma::Vector<2, float>(motion[0], motion[1]).squaredLength() > 0.02f &&
                       motion.dot(creature->m_entityData.acceleration) > 0.0f && !driven) {
                plasma::Vector<2, float> flat(motion[0], motion[1]);
                flat.normalize();
                float &along = flat[1];
                if (along > 1.0f)
                    along = 1.0f;
                if (-1.0f > along)
                    along = -1.0f;
                if (flat[0] > 0.0f)
                    turn = (float)(-acosf(along) / 3.141592653589793 * 180.0);
                else
                    turn = (float)(acosf(along) / 3.141592653589793 * 180.0);
            }
        }
        // The roll: a sound as the local player's roll nears its end.
        if (m_localPlayer && creature->m_entityData.rollTime > RollSoundRemainingTimeMs &&
            creature->m_entityData.rollTime - milliseconds <= RollSoundRemainingTimeMs) {
            ServerUpdates::SoundPacket sound;
            sound.position = creature->m_entityData.position;
            sound.pitch = 1.0f;
            sound.sound = SoundRoll;
            outgoing->sounds.push_back(sound);
        }
        creature->m_rollTime = creature->m_entityData.rollTime;
        if (creature->m_entityData.rollTime != 0)
            creature->m_entityData.rollTime -= milliseconds;
        if (creature->m_entityData.rollTime < 0)
            creature->m_entityData.rollTime = 0;
        // An airship's crew (generationKey: the ship's id) stands at the model's position list 0 and faces
        // the ship's way.
        if (!m_client) {
            long long &shipId = creature->generationKey;
            if (shipId != 0 && m_airships.find(shipId) != m_airships.end()) {
                Sprite *model = m_spriteManager.sprite(SpriteAirship);
                plasma::Matrix<float> placement;
                placement.identity();
                placement.rotateZ(m_airships[shipId]->currentRotation);
                placement.translate((float)model->sizeX() * -0.5f, (float)model->sizeY() * -0.5f,
                                    (float)model->sizeZ() * -0.5f);
                if (!model->m_positionLists[SpritePositionListAirshipCrew].empty()) {
                    plasma::Vector<3, Fixed> up(plasma::Vector<3, float>(0.0f, 0.0f, size[2] * 0.5f + 0.001f));
                    plasma::Vector<3, Fixed> spot(
                        placement *
                        plasma::Vector<3, float>(model->m_positionLists[SpritePositionListAirshipCrew].front()));
                    plasma::Vector<3, Fixed> post = m_airships[shipId]->currentPosition + spot + up;
                    if (plasma::Vector<3, float>(position - post).squaredLength() > 1.0f)
                        position = post;
                }
                turn = m_airships[shipId]->currentRotation;
            }
        }
        // A pet carrying its owner (ability 106) stays under it; otherwise the drawn place and turn follow
        // smoothly (while m_smoothingTime is set) or at once.
        long long &ownerId = creature->m_entityData.ownerId;
        if (creature->m_entityData.hostilityType == HostilityPet && creatureByID(ownerId) &&
            creatureByID(ownerId)->m_entityData.currentAbility == AbilityRidePet) {
            Creature *owner = creatureByID(ownerId);
            float &tall = size[2];
            float &ownerTall = owner->m_entityData.appearance.size[2];
            creature->m_drawnPosition =
                owner->m_drawnPosition +
                plasma::Vector<3, Fixed>(plasma::Vector<3, float>(0.0f, 0.0f, tall * 0.5f - ownerTall * 0.5f + 0.01f));
            creature->m_drawnRotation = owner->m_drawnRotation;
            position =
                owner->m_entityData.position +
                plasma::Vector<3, Fixed>(plasma::Vector<3, float>(0.0f, 0.0f, tall * 0.5f - ownerTall * 0.5f + 0.01f));
            creature->m_entityData.rotation = owner->m_entityData.rotation;
            creature->m_stepOffset = owner->m_stepOffset;
            creature->m_walkAmplitude = owner->m_walkAmplitude;
            creature->m_walkPhase = owner->m_walkPhase;
            motion = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
            creature->m_entityData.acceleration = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
        } else if (creature->m_smoothingTime > 0) {
            float follow = 0.0f;
            float all = 1.0f;
            blend(follow, all, milliseconds, 0.015f);
            plasma::Vector<3, Fixed> &drawn = creature->m_drawnPosition;
            drawn += (position - drawn) * Fixed(follow);
            drawn[2] += (position[2] - drawn[2]) * follow;
            plasma::Vector<3, float> &drawnTurn = creature->m_drawnRotation;
            drawnTurn[0] = interpolateAngle(drawnTurn[0], creature->m_entityData.rotation[0], follow);
            drawnTurn[1] = interpolateAngle(drawnTurn[1], roll, follow);
            drawnTurn[2] = interpolateAngle(drawnTurn[2], turn, follow);
            creature->m_stepOffset = 0.0f;
        } else {
            creature->m_drawnPosition = position;
            creature->m_drawnRotation = creature->m_entityData.rotation;
        }
        // The drawn aim follows the attack direction, and takes it between attacks.
        float share = 0.0f;
        float whole = 1.0f;
        blend(share, whole, milliseconds, 0.025f);
        creature->m_drawnAim += (creature->m_entityData.attackRotation - creature->m_drawnAim) * share;
        if (creature->m_entityData.currentAbility == AbilityNone || time == 0 || time > creature->attackDuration())
            creature->m_drawnAim = creature->m_entityData.attackRotation;
    }

    for (std::map<long long, Zone::Airship *>::iterator it = m_airships.begin(); it != m_airships.end(); it++) {
    }
    // The airships fly their rounds (the server's copy steers them, every copy moves them); their
    // drawn place and turn follow, carrying their riders.
    for (std::map<long long, Zone::Airship *>::iterator it = m_airships.begin(); it != m_airships.end(); it++) {
        Zone::Airship *&ship = it->second;
        if (!ship)
            continue;
        plasma::Vector<3, Fixed> drawnPosition = ship->currentPosition;
        float drawnRotation = ship->currentRotation;
        if (!m_client) {
            if (ship->state == AirshipReturnHome) {
                // Home, slowing down within ten blocks.
                plasma::Vector<3, float> course(ship->home - ship->position);
                float distance = course.squaredLength();
                if (distance < 0.001f) {
                    ship->velocity = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
                    ship->state = AirshipFlyAheadOfHome;
                } else {
                    if (distance > 100.0f) {
                        course.normalize();
                        course *= 10.0f;
                    }
                    ship->velocity = course;
                }
                blend(ship->rotation, interpolateAngle(ship->rotation, ship->heading, 1.0f), milliseconds, 0.005f);
            } else if (ship->state == AirshipFlyBehindHome) {
                // To the point 50 blocks behind home along the heading, 20 blocks up.
                float angle = (float)(-ship->heading * 3.141592653589793 / 180.0);
                plasma::Vector<3, Fixed> target = ship->home + plasma::Vector<3, Fixed>(plasma::Vector<3, float>(
                                                                   sinf(angle) * -50.0f, cosf(angle) * -50.0f, 20.0f));
                plasma::Vector<3, float> course(target - ship->position);
                if (course.squaredLength() < 0.001f) {
                    ship->velocity = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
                    ship->state = AirshipReturnHome;
                } else {
                    course.normalize();
                    course *= 10.0f;
                    ship->velocity = course;
                }
                blend(ship->rotation, interpolateAngle(ship->rotation, ship->heading, 1.0f), milliseconds, 0.005f);
            } else if (ship->state == AirshipFlyAheadOfHome) {
                // The same, 50 blocks ahead of home.
                float angle = (float)(-ship->heading * 3.141592653589793 / 180.0);
                plasma::Vector<3, Fixed> target =
                    ship->home +
                    plasma::Vector<3, Fixed>(plasma::Vector<3, float>(sinf(angle) * 50.0f, cosf(angle) * 50.0f, 20.0f));
                plasma::Vector<3, float> course(target - ship->position);
                if (course.squaredLength() < 0.001f) {
                    ship->velocity = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
                    ship->state = AirshipFlyToDestination;
                } else {
                    course.normalize();
                    course *= 10.0f;
                    ship->velocity = course;
                }
                blend(ship->rotation, interpolateAngle(ship->rotation, ship->heading, 1.0f), milliseconds, 0.005f);
            } else if (ship->state == AirshipFlyToDestination) {
                // To the destination at up to 30 blocks per second, between 100 and 150 blocks above the ground,
                // facing the way.
                plasma::Vector<3, float> course(ship->destination - ship->position);
                float &courseZ = course[2];
                courseZ = 0.0f;
                float distance = course.squaredLength();
                if (distance < 0.001f) {
                    ship->velocity = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
                    ship->state = AirshipFlyBehindHome;
                } else {
                    if (distance > 900.0f) {
                        course.normalize();
                        course *= 30.0f;
                    }
                    blend(ship->velocity[0], course[0], milliseconds, 0.001f);
                    blend(ship->velocity[1], course[1], milliseconds, 0.001f);
                }
                plasma::Vector<3, Fixed> &position = ship->position;
                Field *field = getField((int)position[0], (int)position[1], 0);
                int ground = 0;
                if (field) {
                    ground = field->m_baseZ + field->m_size;
                    if (ground < 0)
                        ground = 0;
                }
                Fixed &height = position[2];
                float climb;
                if (height < ground + 100)
                    climb = 10.0f;
                else if (height > ground + 150)
                    climb = -10.0f;
                else
                    climb = 0.0f;
                blend(ship->velocity[2], climb, milliseconds, 0.001f);
                course = ship->velocity;
                courseZ = 0.0f;
                if (course.squaredLength() > 0.0f) {
                    course.normalize();
                    float &forward = course[1];
                    if (forward > 1.0f)
                        forward = 1.0f;
                    if (-1.0f > forward)
                        forward = -1.0f;
                    float heading;
                    if (course[0] > 0.0f)
                        heading = (float)(-acosf(forward) / 3.141592653589793 * 180.0);
                    else
                        heading = (float)(acosf(forward) / 3.141592653589793 * 180.0);
                    blend(ship->rotation, interpolateAngle(ship->rotation, heading, 1.0f), milliseconds, 0.001f);
                }
            }
        }
        ship->position += plasma::Vector<3, Fixed>(seconds * ship->velocity);
        float follow = 0.0f;
        blend(follow, 1.0f, milliseconds, 0.01f);
        ship->currentPosition += (ship->position - ship->currentPosition) * Fixed(follow);
        blend(ship->currentRotation, interpolateAngle(ship->currentRotation, ship->rotation, 1.0f), milliseconds,
              0.01f);

        // Riders within 64 blocks move and turn with the ship; the others get off.
        std::list<std::set<long long>::iterator> gone;
        for (std::set<long long>::iterator rider = ship->riders.begin(); rider != ship->riders.end(); rider++) {
            Creature *creature = creatureByID(*rider);
            if (!creature ||
                plasma::Vector<3, float>(creature->m_entityData.position - ship->position).squaredLength() > 4096.0f) {
                gone.push_back(rider);
                continue;
            }
            plasma::Matrix<float> turning;
            turning.identity();
            turning.rotateZ(ship->currentRotation - drawnRotation);
            creature->m_drawnPosition =
                ship->currentPosition +
                plasma::Vector<3, Fixed>(turning * plasma::Vector<3, float>(creature->m_drawnPosition - drawnPosition));
            creature->m_drawnRotation[2] += ship->currentRotation - drawnRotation;
            creature->m_entityData.position =
                ship->currentPosition +
                plasma::Vector<3, Fixed>(turning *
                                         plasma::Vector<3, float>(creature->m_entityData.position - drawnPosition));
            creature->m_entityData.rotation[2] += ship->currentRotation - drawnRotation;
        }
        for (std::list<std::set<long long>::iterator>::iterator rider = gone.begin(); rider != gone.end(); rider++)
            ship->riders.erase(*rider);
    }

    // The projectiles: arrows (type 0) and spells (1, 4) fall, the boomerang (2) flies out and back to
    // its owner, a lingering cloud (3) stays. Each moves in steps of at most half a block and hits the
    // hostile creatures whose box it touches.
    std::list<std::list<BulletPacket>::iterator> spent;
    for (std::list<BulletPacket>::iterator it = m_projectiles.begin(); it != m_projectiles.end(); it++) {
        BulletPacket &projectile = *it;
        if (projectile.projectileType == ProjectileMagic) {
        } else if (projectile.projectileType == ProjectileBoomerang) {
            Creature *owner = creatureByID(projectile.ownerId);
            if (!owner || projectile.currentTime > BoomerangLifetimeMs) {
                spent.push_back(it);
                continue;
            }
            if (projectile.currentTime > BoomerangFinalReturnStartMs) {
                float progress = (float)(projectile.currentTime - BoomerangFinalReturnStartMs) / 100.0f;
                projectile.position +=
                    (owner->m_entityData.position - projectile.position) * Fixed(progress) * Fixed(progress);
            } else if (projectile.currentTime > BoomerangReturnStartMs) {
                projectile.velocity =
                    plasma::Vector<3, float>(owner->m_entityData.position - projectile.position) * 10.0f;
            } else {
                plasma::Vector<3, Fixed> aim =
                    owner->m_entityData.position + plasma::Vector<3, Fixed>(owner->m_entityData.attackRotation);
                blend(projectile.velocity, plasma::Vector<3, float>(aim - projectile.position) * 5.0f, milliseconds,
                      0.05f);
            }
        } else {
            projectile.velocity[2] -= seconds * 30.0f * 0.25f;
        }
        plasma::Vector<3, float> step = seconds * projectile.velocity;
        projectile.currentTime += milliseconds;
        // After five seconds it is gone; the owner's attack ends with it.
        if (projectile.projectileType != ProjectileBoomerang && projectile.currentTime > 5000) {
            if (projectile.projectileType != ProjectileCloud && creatureByID(projectile.ownerId)) {
                float tempo = creatureByID(projectile.ownerId)->tempo();
                creatureByID(projectile.ownerId)->m_entityData.hitCombo = 0;
                int &time = creatureByID(projectile.ownerId)->m_entityData.timeSinceAbility;
                time = (int)(tempo / creatureByID(projectile.ownerId)->tempo() * (float)time);
            }
            spent.push_back(it);
            continue;
        }

        Creature *owner = 0;
        if (creatureByID(projectile.ownerId))
            owner = creatureByID(projectile.ownerId);
        int steps = (int)(step.length() * 2.0f + 1.0f);
        plasma::Vector<3, float> stride = step / (float)steps;
        plasma::Vector<3, float> push = projectile.velocity;
        if (push.squaredLength() > 0.0f)
            push.normalize();
        push *= projectile.knockback * 5.0f;
        push[2] = projectile.knockback * 3.0f;
        Creature *attacker = creatureByID(projectile.ownerId);
        for (int i = 0; i < steps; i++) {
            bool finished = false;
            bool hit = false;
            for (std::map<long long, Creature *>::iterator other = m_creatures.begin(); other != m_creatures.end();
                 other++) {
                Creature *target = other->second;
                if (!target || 0.0f >= target->m_entityData.hp)
                    continue;
                // A water (healing) cloud touches everyone; anything else only the attacker's enemies.
                if (!(projectile.projectileType == ProjectileCloud && projectile.element == ElementWater) && attacker &&
                    (target == attacker || !isHostile(attacker, target) || target->m_id == attacker->m_petId))
                    continue;
                plasma::Vector<3, float> &size = target->m_entityData.appearance.size;
                plasma::Vector<3, Fixed> &position = target->m_entityData.position;
                // Half-open boxes.
                if (!(projectile.position[0] + projectile.sizeInBlocks >= position[0] - size[0] * 0.5f) ||
                    !(projectile.position[1] + projectile.sizeInBlocks >= position[1] - size[1] * 0.5f) ||
                    !(projectile.position[2] + projectile.sizeInBlocks >= position[2] - size[2] * 0.5f) ||
                    !(projectile.position[0] - projectile.sizeInBlocks < position[0] + size[0] * 0.5f) ||
                    !(projectile.position[1] - projectile.sizeInBlocks < position[1] + size[1] * 0.5f) ||
                    !(projectile.position[2] - projectile.sizeInBlocks < position[2] + size[2] * 0.5f) ||
                    !inRange(projectile.position, position, true, 200.0f))
                    continue;
                // A rolling target dodges.
                if (target->m_entityData.rollTime != 0) {
                    if (attacker)
                        attacker->m_dodgingCreatures.insert(target->m_id);
                    continue;
                }
                // Unpowered arrows and spells hit at once, the cloud once a second, the returning boomerang every
                // 100 ms.
                int type = projectile.projectileType;
                bool hits = (type == ProjectileArrow && !projectile.powered) ||
                            ((type == ProjectileMagic || type == ProjectileRock) && !projectile.powered) ||
                            (type == ProjectileCloud &&
                             (projectile.currentTime - milliseconds) / 1000 != projectile.currentTime / 1000) ||
                            (projectile.currentTime > BoomerangDamageStartMs && type == ProjectileBoomerang &&
                             (projectile.currentTime - milliseconds) / 100 != projectile.currentTime / 100);
                if (hits) {
                    if (type != ProjectileCloud && type != ProjectileBoomerang)
                        finished = true;
                    // An unpowered arrow or boomerang gives its shooter mana.
                    if (!projectile.powered && type != ProjectileCloud && type != ProjectileMagic &&
                        type != ProjectileRock && attacker) {
                        float gain = (1.0f - (float)rand() * 2.0f / float(RAND_MAX)) * 0.05f + 0.1f;
                        if (projectile.projectileType == ProjectileBoomerang)
                            gain *= 0.2f;
                        gain = (float)attacker->attackDuration() / 300.0f * gain;
                        if (rand() % 8 == 0)
                            gain *= 2.0f;
                        if (!m_client || attacker == m_localPlayer) {
                            attacker->m_entityData.mp += gain;
                            if (attacker->m_entityData.mp > 1.0f)
                                attacker->m_entityData.mp = 1.0f;
                        }
                    }
                    bool critical = false;
                    if (attacker)
                        critical = attacker->rollCritical();
                    float damage = projectile.damage;
                    if (critical)
                        damage *= 2.0f;
                    // A scout's arrow may give it buff 10 for 30 seconds.
                    if (owner &&
                        (owner == m_localPlayer ||
                         (!m_client && owner->m_entityData.hostilityType != HostilityPlayer)) &&
                        owner->hasRandomChance(0.15f) && !projectile.powered &&
                        owner->m_entityData.classType == ClassRanger &&
                        owner->m_entityData.specialization == SpecializationScout) {
                        Creature::Buff buff;
                        buff.type = BuffInstantShoot;
                        buff.timeRemaining = 30000;
                        owner->setBuff(buff);
                        BuffPacket packet;
                        packet.sourceId = owner->m_id;
                        packet.targetId = owner->m_id;
                        packet.buff = buff;
                        outgoing->buffs.push_back(packet);
                        ServerUpdates::SoundPacket sound;
                        sound.sound = SoundUpgrade2;
                        sound.position = owner->m_entityData.position;
                        outgoing->sounds.push_back(sound);
                    }
                    if (0.0f > damage)
                        damage = 0.0f;
                    if (projectile.element == ElementWater) {
                        // A water projectile heals its owner's allies.
                        if (owner && !areEnemies(owner, target) &&
                            target->m_entityData.hostilityType != HostilityAttackableObject) {
                            HitPacket heal;
                            heal.position = target->m_entityData.position;
                            heal.damage = -damage;
                            if (owner->m_entityData.hostilityType == HostilityEnemy)
                                heal.damage *= 0.5f;
                            heal.critical = critical;
                            heal.attackerId = projectile.ownerId;
                            heal.targetId = target->m_id;
                            outgoing->hits.push_back(heal);
                            if (!m_client || (owner == m_localPlayer && target == m_localPlayer))
                                applyHit(heal, outgoing, &changedZones);
                        }
                    } else {
                        hit |= attackCreature(target, attacker, damage, critical, projectile.powered != 0,
                                              projectile.knockback, projectile.position, push, outgoing, &changedZones,
                                              projectile.projectileType == ProjectileMagic, 0, &*it, true);
                    }
                    if (projectile.slowedTime > target->m_entityData.slowedTime)
                        target->m_entityData.slowedTime = projectile.slowedTime;
                }
                if (projectile.projectileType == ProjectileArrow || projectile.projectileType == ProjectileMagic ||
                    projectile.projectileType == ProjectileRock) {
                    finished = true;
                    break;
                }
                if (finished)
                    break;
            }
            if (projectile.projectileType == ProjectileCloud)
                continue;
            if (projectile.projectileType == ProjectileBoomerang &&
                (projectile.currentTime - milliseconds) / BoomerangSwingSoundPeriodMs !=
                    projectile.currentTime / BoomerangSwingSoundPeriodMs) {
                ServerUpdates::SoundPacket sound;
                sound.position = projectile.position;
                sound.sound = SoundSwing;
                sound.pitch = (float)rand() * 0.25f / float(RAND_MAX) + 1.0f;
                outgoing->sounds.push_back(sound);
            }
            plasma::Vector<3, Fixed> &position = projectile.position;
            position += plasma::Vector<3, Fixed>(stride);
            float size = projectile.sizeInBlocks;
            plasma::Vector<3, Fixed> lower =
                position - plasma::Vector<3, Fixed>(plasma::Vector<3, float>(size, size, size));
            plasma::Vector<3, Fixed> upper =
                position + plasma::Vector<3, Fixed>(plasma::Vector<3, float>(size, size, size));
            // It stops at a solid block (an arrow raising dust of the block's colour) or at an obstacle on the
            // way back.
            bool blocked = false;
            Fixed &z = position[2];
            Fixed &y = position[1];
            Fixed &x = position[0];
            if (getBlock(x, y, z, 0).isSolid()) {
                position -= plasma::Vector<3, Fixed>(stride);
                blocked = true;
                if (projectile.projectileType == ProjectileArrow) {
                    ServerUpdates::ParticlePacket dust;
                    plasma::Vector<3, float> color = plasma::Vector<3, float>(getBlock(x, y, z, 0).color) / 255.0f;
                    dust.color = plasma::Vector<4, float>(color[0], color[1], color[2], 1.0f);
                    dust.acceleration = plasma::Vector<3, float>(0.0f, 0.0f, 10.0f);
                    dust.count = ArrowDustParticleCount;
                    dust.size = 0.1f;
                    dust.position = position;
                    outgoing->particles.push_back(dust);
                }
            } else {
                float travel = projectile.velocity.length() * seconds;
                if (travel > rayLength(position, -projectile.velocity.normalized(), travel, false, true)) {
                    blocked = true;
                    position -= plasma::Vector<3, Fixed>(stride);
                }
            }
            // A blocked shot ends the shooter's attack.
            if (!finished && blocked && projectile.projectileType != ProjectileCloud && attacker) {
                float tempo = attacker->tempo();
                attacker->m_entityData.hitCombo = 0;
                attacker->m_entityData.timeSinceAbility =
                    (int)(tempo / attacker->tempo() * (float)attacker->m_entityData.timeSinceAbility);
            }
            if (projectile.projectileType == ProjectileBoomerang) {
                if (hit && attacker) {
                    attacker->m_entityData.hitCombo++;
                    attacker->onHit();
                    attacker->m_entityData.timeSinceHit = 0;
                }
                if (blocked) {
                    if (projectile.projectileType == ProjectileBoomerang) {
                        projectile.velocity = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
                    } else {
                        ServerUpdates::SoundPacket sound;
                        sound.position = projectile.position;
                        sound.sound = SoundArrowDestroy;
                        sound.pitch = (float)rand() * 0.4f / float(RAND_MAX) + 1.0f;
                        outgoing->sounds.push_back(sound);
                        spent.push_back(it);
                        break;
                    }
                }
                continue;
            }
            if (!finished && !blocked)
                continue;
            // A spell or a powered shot bursts: a sound, then it hits every hostile creature within five
            // blocks.
            if (projectile.projectileType == ProjectileMagic || projectile.powered) {
                if (!finished) {
                    ServerUpdates::SoundPacket sound;
                    sound.position = projectile.position;
                    sound.pitch = (float)rand() * 0.4f / float(RAND_MAX) + 1.0f;
                    if (projectile.projectileType == ProjectileMagic)
                        sound.sound = projectile.element == ElementWater ? SoundWatersplashHit : SoundFireHit;
                    else
                        sound.sound = SoundPoweredProjectileHit;
                    outgoing->sounds.push_back(sound);
                }
                bool powered = projectile.powered != 0;
                for (std::map<long long, Creature *>::iterator other = m_creatures.begin(); other != m_creatures.end();
                     other++) {
                    Creature *target = other->second;
                    if (!target)
                        continue;
                    if (owner && !m_client && owner->m_entityData.hostilityType == HostilityPlayer &&
                        owner != m_localPlayer)
                        break;
                    if (attacker && (target == attacker || !isHostile(attacker, target) ||
                                     target->m_id == attacker->m_petId || 0.0f >= target->m_entityData.hp))
                        continue;
                    plasma::Vector<3, float> &size = target->m_entityData.appearance.size;
                    plasma::Vector<3, Fixed> &position = target->m_entityData.position;
                    // Half-open boxes.
                    if (!(projectile.position[0] + 5.0f >= position[0] - size[0] * 0.5f) ||
                        !(projectile.position[1] + 5.0f >= position[1] - size[1] * 0.5f) ||
                        !(projectile.position[2] + 5.0f >= position[2] - size[2] * 0.5f) ||
                        !(projectile.position[0] - 5.0f < position[0] + size[0] * 0.5f) ||
                        !(projectile.position[1] - 5.0f < position[1] + size[1] * 0.5f) ||
                        !(projectile.position[2] - 5.0f < position[2] + size[2] * 0.5f) ||
                        !inRange(projectile.position, position, true, 200.0f))
                        continue;
                    if (target->m_entityData.rollTime != 0 && attacker) {
                        attacker->m_dodgingCreatures.insert(target->m_id);
                        continue;
                    }
                    bool critical = false;
                    if (owner && owner->rollCritical())
                        critical = true;
                    float damage = projectile.damage;
                    if (critical)
                        damage *= 2.0f;
                    // A fire spell may give its caster buff 9.
                    if (owner &&
                        (owner == m_localPlayer ||
                         (!m_client && owner->m_entityData.hostilityType != HostilityPlayer)) &&
                        owner->hasRandomChance(0.25f) && !powered && projectile.projectileType == ProjectileMagic &&
                        projectile.element == ElementFire) {
                        Creature::Buff buff;
                        buff.type = BuffInstantFire;
                        buff.timeRemaining = 30000;
                        owner->setBuff(buff);
                        BuffPacket packet;
                        packet.sourceId = owner->m_id;
                        packet.targetId = owner->m_id;
                        packet.buff = buff;
                        outgoing->buffs.push_back(packet);
                        ServerUpdates::SoundPacket sound;
                        sound.sound = SoundUpgrade2;
                        sound.position = owner->m_entityData.position;
                        outgoing->sounds.push_back(sound);
                    }
                    hit |= attackCreature(target, attacker, damage, critical, powered, projectile.knockback,
                                          projectile.position, push, outgoing, &changedZones,
                                          projectile.projectileType == ProjectileMagic, 0, &*it, true);
                }
            }
            if (hit && projectile.projectileType != ProjectileCloud && attacker) {
                attacker->m_entityData.hitCombo++;
                attacker->onHit();
                attacker->m_entityData.timeSinceHit = 0;
            }
            // A water spell becomes a lingering healing cloud.
            if (projectile.projectileType == ProjectileMagic && projectile.element == ElementWater) {
                projectile.projectileType = ProjectileCloud;
                projectile.currentTime = projectile.powered ? 0 : UnpoweredWaterCloudInitialAgeMs;
                projectile.damage *= 0.05f;
                projectile.sizeInBlocks = projectile.knockback * 5.0f + 5.0f;
                projectile.knockback = 0.0f;
                projectile.powered = 0;
            } else {
                spent.push_back(it);
            }
            break;
        }
    }
    for (std::list<std::list<BulletPacket>::iterator>::iterator it = spent.begin(); it != spent.end(); it++)
        m_projectiles.erase(*it);

    // A pet carrying its owner (ability 106) stays under it.
    for (std::map<long long, Creature *>::iterator it = m_creatures.begin(); it != m_creatures.end(); it++) {
        Creature *&creature = it->second;
        if (!creature)
            continue;
        long long &target = creature->m_opponentId;
        if (creatureByID(target) && 0.0f >= creatureByID(target)->m_entityData.hp)
            target = 0;
        Creature *pet = creature;
        if (pet->m_entityData.hostilityType != HostilityPet)
            continue;
        long long &ownerId = pet->m_entityData.ownerId;
        if (!creatureByID(ownerId) || creatureByID(ownerId)->m_entityData.currentAbility != AbilityRidePet)
            continue;
        Creature *owner = creatureByID(ownerId);
        float &petHeight = pet->m_entityData.appearance.size[2];
        float &ownerHeight = owner->m_entityData.appearance.size[2];
        pet->m_drawnPosition = owner->m_drawnPosition + plasma::Vector<3, Fixed>(plasma::Vector<3, float>(
                                                            0.0f, 0.0f, petHeight * 0.5f - ownerHeight * 0.5f + 0.01f));
        pet->m_drawnRotation = owner->m_drawnRotation;
        pet->m_entityData.position =
            owner->m_entityData.position + plasma::Vector<3, Fixed>(plasma::Vector<3, float>(
                                               0.0f, 0.0f, petHeight * 0.5f - ownerHeight * 0.5f + 0.01f));
        pet->m_entityData.rotation = owner->m_entityData.rotation;
        pet->m_stepOffset = owner->m_stepOffset;
        pet->m_walkAmplitude = owner->m_walkAmplitude;
        pet->m_walkPhase = owner->m_walkPhase;
        pet->m_entityData.velocity = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
        pet->m_entityData.acceleration = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
    }

    // The zones whose items changed are saved and sent; the zones of the object and block changes take
    // note.
    for (std::set<Zone *>::iterator it = changedZones.begin(); it != changedZones.end(); it++) {
        (*it)->m_modified = true;
        outgoing->items.push_back(ServerUpdates::ItemListPacket(**it));
    }
    std::list<ServerUpdates::ZoneObjectPacket> &objects = outgoing->objects;
    for (std::list<ServerUpdates::ZoneObjectPacket>::iterator it = objects.begin(); it != objects.end(); it++) {
        ServerUpdates::ZoneObjectPacket &packet = *it;
        Zone *zone = this->zone(packet.key[0], packet.key[1]);
        if (zone)
            zone->m_objectsChanged = true;
    }
    for (std::set<Zone *>::iterator it = buildingZones.begin(); it != buildingZones.end(); it++)
        outgoing->buildings.push_back(ServerUpdates::BuildingListPacket(**it));
    std::list<ServerUpdates::BlockPacket> &blocks = outgoing->blocks;
    for (std::list<ServerUpdates::BlockPacket>::iterator it = blocks.begin(); it != blocks.end(); it++) {
        ServerUpdates::BlockPacket &change = *it;
        Zone *zone = this->zone(change.position[0] / BlocksPerZone, change.position[1] / BlocksPerZone);
        if (zone)
            zone->m_blockChanges.push_back(*it);
    }
}

}
