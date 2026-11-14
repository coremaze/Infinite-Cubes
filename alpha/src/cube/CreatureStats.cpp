// Creature statistics, race appearances and equipment-dependent attributes.
#include <math.h>
#include <stdlib.h>
#include "cube/Creature.h"
#include "cube/Behavior.h"
#include "cube/CreatureIds.h"
#include "cube/StatCurves.h"
#include "cube/RaceIds.h"
#include "cube/ServerUpdates.h"

#include "cube/SpriteIds.h"

namespace cube {

namespace {
enum {
    MaximumCarriedConsumableOrMaterialQuantity = 50,
    ManaCubeModifierCycleLength = 100,
    LargeSpawnGroupMaximum = 5,
    SmallSpawnGroupMaximum = 3,
    PlayerStunDurationMs = 1500,
    NpcBaseStunDurationMs = 3000,
    NpcRarityStunReductionMs = 1500,
    NpcRarityStunReductionSteps = 4,
    ComboContinuationWindowMs = 200,
    TeleportToCityAnticipationMs = 5000,
    InterceptGroupAnticipationMs = 100,
    SmashAnticipationMs = 400,
    ShellBlockGroupAnticipationMs = 200,
    NoneGroupHoldMs = 100,
    BlockJabHoldMs = 600,
    SmashHoldMs = 400,
    ShellBlockGroupHoldMs = 200,
    RetreatGroupStrikeMs = 500,
    SmashStrikeMs = 100,
    BlockJabStrikeMs = 200,
    FireSwirlGroupStrikeMs = 600,
    ShellBlockStrikeMs = 3000,
    ShellSpinGroupStrikeMs = 5000,
    QuakeStrikeMs = 6000,
    HeadbuttGroupStrikeMs = 1000,
    FireRayStrikeMs = 2000,
    FireVortexGroupStrikeMs = 1200,
    ShellSpinCooldownMs = 15000,
    ElfHeadM01VariantCount = 4,
    ElfHairM01VariantCount = 10,
    HumanHeadM01VariantCount = 6,
    HumanHairM01VariantCount = 15,
    HumanHeadF01VariantCount = 6,
    HumanHairF01VariantCount = 7,
    FrogmanHeadM01VariantCount = 5,
    FrogmanHairM01VariantCount = 5,
    FrogmanHeadF01VariantCount = 4,
    FrogmanHairF01VariantCount = 4,
    ElfHeadF01VariantCount = 6,
    ElfHairF01VariantCount = 10,
    GoblinHeadM01VariantCount = 5,
    GoblinHairM01VariantCount = 6,
    GoblinHeadF01VariantCount = 5,
    GoblinHairF01VariantCount = 6,
    BullterrierHead1VariantCount = 3,
    LizardHeadM01VariantCount = 2,
    LizardHairM01VariantCount = 6,
    LizardHeadF01VariantCount = 5,
    MermaidHeadF01VariantCount = 3,
    MermaidHairF01VariantCount = 3,
    MermanHeadM01VariantCount = 3,
    MermanHairM01VariantCount = 3,
    DwarfHeadM01VariantCount = 5,
    DwarfHairM01VariantCount = 3,
    DwarfHeadF01VariantCount = 5,
    DwarfHairF01VariantCount = 5,
    OrcHeadM01VariantCount = 5,
    OrcHairM01VariantCount = 10,
    OrcHeadF01VariantCount = 5,
    OrcHairF01VariantCount = 4,
    RuneGiantBody01VariantCount = 2,
    UndeadHeadM01VariantCount = 6,
    UndeadHairM01VariantCount = 6,
    UndeadHeadF01VariantCount = 6,
    UndeadHairF01VariantCount = 6,
    DesertNomadHead1VariantCount = 3,
    DesertNomadFemaleHead1VariantCount = 3,
    MonsterHeadReptileLizardVariantCount = 5,
    MonsterBodyReptileShellVariantCount = 9,
    MonsterFootReptileNormalVariantCount = 3,
    MonsterTailReptileNormalVariantCount = 2,
    CharacterStyleChoiceCount = 100,
};
}

float skillBonus(int points)
{
    if (points < 1)
        return 0.0f;
    return 1.0f - 1.0f / (points * 0.1f + 1.0f);
}

int abilitySkillIndex(int ability)
{
    switch (ability) {
    case AbilityRangerKick:
    case AbilityHealingStream:
    case AbilityIntercept:
    case AbilitySmash:
    case AbilityFireExplosion:
        return SkillClassAbilityFirst;
    case AbilityRetreat:
    case AbilitySneak:
    case AbilityCyclone:
    case AbilityManaShield:
        return SkillClassAbilitySecond;
    case AbilityTeleport:
    case AbilityShuriken:
    case AbilityCamouflage:
    case AbilityAim:
    case AbilitySwiftness:
    case AbilityBulwark:
    case AbilityWarFrenzy:
        return SkillClassAbilityThird;
    default:
        return -1;
    }
}

float levelBonus(float level)
{
    return 1.0f - 1.0f / ((level - 1.0f) * 0.05f + 1.0f);
}

int Creature::anticipationTime(int ability)
{
    if (ability < 0)
        ability = m_entityData.currentAbility;
    switch (ability) {
    case AbilityTeleportToCity:
        return TeleportToCityAnticipationMs;
    case AbilityIntercept:
    case AbilityBulwark:
        return InterceptGroupAnticipationMs;
    case AbilityNone:
    case AbilityBlock:
    case AbilitySwirlStrike:
    case AbilitySupportRay:
    case AbilityRetreat:
    case AbilitySalvoRelease:
    case AbilityCrushStrike:
    case AbilityGuardianCrushStrike:
    case Ability62:
    case AbilityShuriken:
    case AbilityParry:
    case AbilityBlockStrike:
        return 0;
    case AbilityChargedShot:
    case AbilitySalvo:
    case AbilityChargeBoomerang:
    case AbilityChargeFireball:
    case AbilityCrush:
    case AbilitySwirl:
    case AbilityMonsterCharge:
        return (int)(50.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityExplosion:
        return (int)(5000.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityFireRay:
        return (int)(1000.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityFireBeam:
        return (int)(500.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityFireball:
    case AbilityWaterSplash:
    case AbilityLava:
        if (m_entityData.equipment.weaponRight.id == WeaponBracelet)
            return (int)(600.0f / (tempo() * m_entityData.multipliers.attackSpeed));
        return (int)(1200.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityWaterSalvo:
    case AbilityFireSalvo:
        return (int)(1200.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityFirebolt:
    case AbilityBraceletFirebolt:
    case AbilityWaterDrop:
    case AbilityBraceletWaterDrop:
        return (int)(200.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityWandFirebolt:
    case AbilityWandWaterDrop:
        return (int)(500.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityFireSwirl:
    case AbilityWaterSwirl:
        return (int)(800.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityFireVortex:
    case AbilityWaterVortex:
        return (int)(1600.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityHealingStream:
        return (int)(1600.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityShoot:
        return (int)(400.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityChargedShotRelease:
        return (int)(50.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityLeftSlash:
    case Ability3:
    case Ability4:
        return (int)(100.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityBlockJab:
        return (int)(100.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityPerforate:
        return (int)(100.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityRightSlash:
    case AbilityShieldAttack:
        return (int)(300.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityTwoHandedSpin:
    case AbilityTwoHandedSlam:
        return (int)(300.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityTwoHandedSweep:
    case AbilityTwoHandedThrust:
        return (int)(800.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityCrossedWeaponSweep:
    case AbilityMonsterChargeStrike:
    case AbilityTwoHandedSwing:
        return (int)(200.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityRightStab:
    case AbilityLeftStab:
        return (int)(50.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityAmbush:
    case AbilityLeftDaggerPierce:
    case AbilityRightDaggerPierce:
    case AbilityKick:
    case AbilityRangerKick:
        return (int)(100.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityLeftSweep:
        return (int)(200.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityRightSweep:
        return (int)(100.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityOverheadChop:
        return (int)(200.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case Ability93:
        return (int)(800.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityThrowBoomerang:
        return (int)(300.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityHeadbutt:
    case AbilityHornCharge:
    case AbilityTailStrike:
    case AbilityScream:
    case AbilityStomp:
    case AbilityBite:
    case AbilityDashForward:
    case AbilityDashLeft:
    case AbilityDashRight:
        return (int)(300.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityQuake:
        return (int)(1000.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilitySmash:
        return SmashAnticipationMs;
    case AbilityShellBlock:
    case AbilityShellSpin:
        return ShellBlockGroupAnticipationMs;
    default:
        return (int)(400.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    }
}

// A rolling ninja (rogue of specialization 1) regains a quarter of its mana and gets a guaranteed
// critical hit for 30 seconds, queued for the clients.
void Creature::applyDodgeBonus(ServerUpdates *updates)
{
    if (m_entityData.rollTime != 0 && m_entityData.classType == ClassRogue &&
        m_entityData.specialization == SpecializationNinja) {
        m_entityData.mp += 0.25f;
        if (m_entityData.mp > 1.0f)
            m_entityData.mp = 1.0f;
        Buff buff;
        buff.type = BuffGuaranteedCritical;
        buff.timeRemaining = 30000;
        setBuff(buff);
        BuffPacket packet;
        packet.buff = buff;
        packet.sourceId = m_id;
        packet.targetId = m_id;
        updates->buffs.push_back(packet);
    }
}

float Creature::armor()
{
    float levelScale = (float)pow(2.0, (double)(levelBonus((float)m_entityData.level) * 3.0f));
    float scale = levelScale * (float)pow(2.0, (double)(m_entityData.rarity * 0.25f));
    float armor = m_entityData.multipliers.armor * scale;
    if (m_entityData.hostilityType == HostilityPlayer)
        armor = (float)pow(2.0, 1.0) * levelScale * m_entityData.multipliers.armor;
    if (m_entityData.appearance.appearanceFlags & (1 << AppearanceFlagUnarmored))
        armor += scale;
    Equipment &equipment = m_entityData.equipment;
    if (equipment.chest.category == ItemCategoryChest)
        armor += equipment.chest.armor();
    if (equipment.feet.category == ItemCategoryFeet)
        armor += equipment.feet.armor();
    if (equipment.hands.category == ItemCategoryHands)
        armor += equipment.hands.armor();
    if (equipment.shoulder.category == ItemCategoryShoulder)
        armor += equipment.shoulder.armor();
    return armor;
}

int Creature::attackDuration()
{
    return anticipationTime() + strikeTime() + holdTime();
}

float Creature::attackPower()
{
    float damage = m_entityData.multipliers.damage * itemPowerScale(m_entityData.level, ItemRarityWhite);
    std::list<Item *> weapons = attackWeapons();
    if (weapons.empty()) {
        // Unarmed creatures hit with their level and rarity instead.
        if (m_entityData.appearance.appearanceFlags & (1 << AppearanceFlagHandAttacks))
            damage += itemPowerScale(m_entityData.level, m_entityData.rarity) * 2.0f;
        if (m_entityData.appearance.appearanceFlags & (1 << AppearanceFlagBiteAttack))
            damage += itemPowerScale(m_entityData.level, m_entityData.rarity) * 8.0f;
    } else {
        for (std::list<Item *>::iterator it = weapons.begin(); it != weapons.end(); ++it)
            damage += (*it)->weaponDamage();
    }
    switch (m_entityData.currentAbility) {
    case AbilityExplosion:
        damage *= 50.0f;
        break;
    case AbilityQuake:
        damage *= 2.0f;
        break;
    case Ability93:
        damage *= 5.0f;
        break;
    case AbilityShuriken:
        damage *= 3.0f;
        break;
    }
    return damage;
}

std::list<Item *> Creature::attackWeapons()
{
    std::list<Item *> weapons;
    // A bow or crossbow is the only weapon of any attack.
    if (m_entityData.equipment.weaponRight.category == ItemCategoryWeapon &&
        (m_entityData.equipment.weaponRight.id == WeaponBow ||
         m_entityData.equipment.weaponRight.id == WeaponCrossbow)) {
        weapons.push_back(&m_entityData.equipment.weaponRight);
        return weapons;
    }
    if (m_entityData.equipment.weaponLeft.category == ItemCategoryWeapon &&
        (m_entityData.equipment.weaponLeft.id == WeaponBow || m_entityData.equipment.weaponLeft.id == WeaponCrossbow)) {
        weapons.push_back(&m_entityData.equipment.weaponLeft);
        return weapons;
    }
    switch (m_entityData.currentAbility) {
    case AbilityLeftSlash:
    case Ability4:
    case AbilityLeftStab:
    case AbilityBlock:
    case AbilityBlockJab:
    case AbilityLeftDaggerPierce:
    case AbilityFirebolt:
    case AbilityWaterDrop:
        if (m_entityData.equipment.weaponLeft.category == ItemCategoryWeapon)
            weapons.push_back(&m_entityData.equipment.weaponLeft);
        break;
    case AbilityBraceletFirebolt:
    case AbilityBraceletWaterDrop:
        if (m_entityData.equipment.weaponRight.category == ItemCategoryWeapon)
            weapons.push_back(&m_entityData.equipment.weaponRight);
        break;
    case AbilitySwirlStrike:
    case AbilityCrossedWeaponSweep:
    case AbilityMonsterChargeStrike:
    case AbilityAmbush:
    case AbilityKick:
    case AbilitySmash:
    case AbilityCyclone:
    case AbilityQuake:
    case AbilityShuriken:
        if (m_entityData.equipment.weaponLeft.category == ItemCategoryWeapon)
            weapons.push_back(&m_entityData.equipment.weaponLeft);
        if (m_entityData.equipment.weaponRight.category == ItemCategoryWeapon)
            weapons.push_back(&m_entityData.equipment.weaponRight);
        break;
    default:
        if (m_entityData.equipment.weaponRight.category == ItemCategoryWeapon)
            weapons.push_back(&m_entityData.equipment.weaponRight);
        if (m_entityData.equipment.weaponLeft.category == ItemCategoryWeapon &&
            m_entityData.equipment.weaponLeft.id == WeaponBracelet)
            weapons.push_back(&m_entityData.equipment.weaponLeft);
        break;
    }
    return weapons;
}

float Creature::blockPower()
{
    float power = itemPowerScale(m_entityData.level, ItemRarityWhite) * m_entityData.multipliers.damage;
    Equipment &equipment = m_entityData.equipment;
    if (equipment.weaponLeft.category == ItemCategoryWeapon && equipment.weaponLeft.id == WeaponShield) {
        power = equipment.weaponRight.weaponDamage() * 4.0f + power;
    } else {
        if (equipment.weaponLeft.category == ItemCategoryWeapon)
            power = equipment.weaponLeft.weaponDamage() + power;
        if (equipment.weaponRight.category == ItemCategoryWeapon)
            power = equipment.weaponRight.weaponDamage() + power;
    }
    return m_entityData.multipliers.damage * power;
}

float bonusFromLevel(int points)
{
    if (points < 1)
        return 0.0f;
    return (1.0f - 1.0f / (points * 0.1f + 1.0f)) * 3.0f + 1.0f;
}

bool Creature::canBeDamaged()
{
    unsigned char hostility = m_entityData.hostilityType;
    return hostility == HostilityPlayer || hostility == HostilityEnemy || hostility == HostilityFriendlyNpc ||
           hostility == HostilityPet ||
           (hostility == HostilityAttackableObject && m_entityData.race != RaceScarecrow &&
            m_entityData.race != RaceDummy && m_entityData.race != RaceAim);
}

// Whether the right button's skill may start now (GameController::update): not while intercepting,
// and only once the attack in progress has run its course or is one of the skills it may
// interrupt, with no charge built up.
bool Creature::canCancelAbility()
{
    const unsigned char &ability = m_entityData.currentAbility;
    if (ability != AbilityIntercept &&
        (m_entityData.timeSinceAbility >= attackDuration() || ability == AbilityNone || ability == AbilityParry ||
         ability == AbilityBulwark || ability == AbilityCyclone || ability == AbilityBlockJab ||
         ability == AbilityLeftSlash || ability == AbilityRightSlash || ability == AbilityShieldAttack ||
         ability == Ability4 || ability == Ability3 || ability == AbilityLeftSweep || ability == AbilityRightSweep ||
         ability == AbilityOverheadChop || ability == AbilityLeftDaggerPierce || ability == AbilityRightDaggerPierce ||
         ability == AbilityLeftStab || ability == AbilityRightStab || ability == AbilityMonsterChargeStrike ||
         ability == AbilityCrossedWeaponSweep || ability == AbilityTwoHandedSweep ||
         ability == AbilityTwoHandedThrust || ability == AbilityTwoHandedSwing || ability == AbilityTwoHandedSpin ||
         ability == AbilityTwoHandedSlam || ability == AbilityShoot || ability == AbilityThrowBoomerang ||
         ability == AbilityFireball || ability == AbilityFireSalvo || ability == AbilityWaterSalvo ||
         ability == AbilityFireVortex || ability == AbilityWaterVortex || ability == AbilityHealingStream ||
         ability == AbilitySupportRay || ability == Ability29) &&
        m_entityData.charge <= 0.0f)
        return true;
    return false;
}

bool Creature::canEquipItem(const Item &item)
{
    int maximumPower = (int)(levelBonus((float)m_entityData.level) * 100.0f + 1.0f);
    if (item.power() > maximumPower)
        return false;
    return item.fitsClass(m_entityData.classType) ? true : false;
}

bool Creature::canTakeItem(const Item &item)
{
    if (item.category == ItemCategoryConsumable || item.category == ItemCategoryMaterial ||
        item.category == ItemCategoryPetFood) {
        int quantity = 0;
        if (m_inventory.m_cursor.item == item)
            quantity = m_inventory.m_cursor.quantity;
        for (std::vector<std::vector<ItemStack>>::iterator tab = m_inventory.m_inventoryTabs.begin();
             tab != m_inventory.m_inventoryTabs.end(); ++tab) {
            for (std::vector<ItemStack>::iterator stack = tab->begin(); stack != tab->end(); ++stack) {
                if (stack->item == item)
                    quantity += stack->quantity;
            }
        }
        if (item.category == ItemCategoryPetFood && quantity >= 1)
            return false;
        if (quantity >= MaximumCarriedConsumableOrMaterialQuantity)
            return false;
    }
    return true;
}

bool Creature::canUseFormula(const Item &item)
{
    if (item.category == ItemCategoryManaCube) {
        int points = m_entityData.manaCubeCount;
        if (points > ManaCubeModifierCycleLength)
            points = points + ManaCubeModifierCycleLength -
                     (points / ManaCubeModifierCycleLength) * ManaCubeModifierCycleLength;
        return item.modifier == points;
    }
    return true;
}

bool Creature::canUseSkill(int ability)
{
    if (m_entityData.stunTime > 0 && ability != AbilityBulwark)
        return false;
    if (manaCost(ability, -1) > m_entityData.mp)
        return false;
    std::map<int, int>::iterator cooldown = m_skillCooldowns.find(ability);
    if (cooldown != m_skillCooldowns.end() && cooldown->second != 0)
        return false;
    if (ability != AbilitySupportRay)
        return true;
    return m_entityData.mp > 0.0f;
}

float climbingSpeed(int points)
{
    return 1.0f - 1.0f / ((points + 1) * 0.1f + 1.0f);
}

int Creature::cooldown(int ability, int points)
{
    switch (ability) {
    case AbilityIntercept:
        return (int)(20000.0f - abilitySkillBonus(ability, points) * 12000.0f);
    case AbilityTeleport:
        return (int)(16000.0f - abilitySkillBonus(ability, points) * 10000.0f);
    case AbilityBulwark:
        return (int)(60000.0f - abilitySkillBonus(ability, points) * 30000.0f);
    case AbilityWarFrenzy:
        return (int)(60000.0f - abilitySkillBonus(ability, points) * 30000.0f);
    case AbilityCamouflage:
        return (int)(60000.0f - abilitySkillBonus(ability, points) * 30000.0f);
    case AbilityCyclone:
        return (int)(60000.0f - abilitySkillBonus(ability, points) * 40000.0f);
    case AbilitySmash:
        return (int)(20000.0f - abilitySkillBonus(ability, points) * 14000.0f);
    case AbilityRetreat:
        return (int)(16000.0f - abilitySkillBonus(ability, points) * 10000.0f);
    case AbilityAim:
        return (int)(12000.0f - abilitySkillBonus(ability, points) * 10000.0f);
    case AbilitySwiftness:
        return (int)(60000.0f - abilitySkillBonus(ability, points) * 30000.0f);
    case AbilityShuriken:
        return (int)(20000.0f - abilitySkillBonus(ability, points) * 14000.0f);
    case AbilityManaShield:
        return (int)(20000.0f - abilitySkillBonus(ability, points) * 10000.0f);
    case AbilityShellSpin:
        return ShellSpinCooldownMs;
    case AbilityRangerKick:
        return (int)(8000.0f - abilitySkillBonus(ability, points) * 8000.0f);
    case AbilityFireExplosion:
        return (int)(8000.0f - abilitySkillBonus(ability, points) * 8000.0f);
    default:
        return 0;
    }
}

float Creature::criticalChance()
{
    if (findBuff(BuffGuaranteedCritical))
        return 1.0f;
    return critical() + m_stealth * 0.15f;
}

float Creature::critical()
{
    float chance = itemDamageScale((float)m_entityData.level, ItemRarityWhite) * 0.1f;
    Equipment &equipment = m_entityData.equipment;
    if (equipment.weaponLeft.category == ItemCategoryWeapon)
        chance += equipment.weaponLeft.critical();
    if (equipment.weaponRight.category == ItemCategoryWeapon)
        chance += equipment.weaponRight.critical();
    if (equipment.chest.category == ItemCategoryChest)
        chance += equipment.chest.critical();
    if (equipment.feet.category == ItemCategoryFeet)
        chance += equipment.feet.critical();
    if (equipment.hands.category == ItemCategoryHands)
        chance += equipment.hands.critical();
    if (equipment.shoulder.category == ItemCategoryShoulder)
        chance += equipment.shoulder.critical();
    if (equipment.neck.category == ItemCategoryNeck)
        chance += equipment.neck.critical();
    if (equipment.ringLeft.category == ItemCategoryRing)
        chance += equipment.ringLeft.critical();
    if (equipment.ringRight.category == ItemCategoryRing)
        chance += equipment.ringRight.critical();
    return chance;
}

float Creature::damageScale(int ability)
{
    switch (ability) {
    case AbilityFireball:
    case AbilityWaterSplash:
    case AbilityTwoHandedSweep:
    case AbilityTwoHandedThrust:
    case AbilityHeadbutt:
        return 2.0f;
    case AbilitySalvoRelease:
        return 0.25f;
    case AbilityKick:
    case AbilityWandFirebolt:
    case AbilityFirebolt:
    case AbilityBraceletFirebolt:
    case AbilityWaterDrop:
    case AbilityBraceletWaterDrop:
    case AbilityWandWaterDrop:
    case AbilityWaterSalvo:
    case AbilityFireSalvo:
    case AbilitySmash:
    case AbilityTwoHandedSwing:
        return 1.0f;
    case AbilityPerforate:
    case AbilityThrowBoomerang:
        return 0.5f;
    case AbilityFireSwirl:
    case AbilityWaterSwirl:
        return 0.4f;
    case AbilityFireVortex:
    case AbilityWaterVortex:
        return 0.6f;
    case AbilityRangerKick:
    case AbilityShellSpin:
        return 0.1f;
    default:
        return 1.0f;
    }
}

float Creature::abilitySkillBonus(int ability, int points)
{
    int index = abilitySkillIndex(ability);
    if (index < 0)
        return 1.0f;
    if (points < 0)
        points = m_entityData.skillLevels[index];
    if (points == 0 && m_entityData.hostilityType != HostilityPlayer)
        points = m_entityData.level / 2 + m_entityData.rarity + 1;
    return 1.0f - 1.0f / (points * 0.1f + 1.0f);
}

float hangGlidingSpeed(int points)
{
    if (points < 1)
        return 0.0f;
    return (1.0f - 1.0f / (points * 0.1f + 1.0f)) * 3.0f + 1.5f;
}

// One case per race, in the order of  case blocks (the race values are not ascending).
// Without a style (generated creatures) the hair colour is random and the face and haircut are
// rolled from 0..99; a player's style also chooses the race (CharacterStyleWidget's race choices:
// Human, Elf, Dwarf, Orc, Goblin, Lizard, Undead, Frogman). Model numbers are indices of the
// game's sprite table; the flags are Appearance::appearanceFlags bits, mostly not understood. Race
// appearance table: sprite IDs, model-space part offsets, scales and rotations; each race keeps
// its authored proportions.
void fillAppearance(int &race, Creature::Appearance &appearance, const Creature::PlayerData *playerStyle)
{
    Creature::CharacterStyle randomStyle;
    const Creature::CharacterStyle *style = playerStyle;
    if (!style) {
        // The original draws b, g, r in that order.
        randomStyle.hairColor = plasma::Vector<3, unsigned char>(rand() % 256, rand() % 256, rand() % 256);
        randomStyle.face = rand() % CharacterStyleChoiceCount;
        randomStyle.haircut = rand() % CharacterStyleChoiceCount;
        style = &randomStyle;
    } else {
        switch (style->race) {
        case CharacterRaceChoiceHuman:
            race = style->gender % 2 + RaceHumanMale;
            break;
        case CharacterRaceChoiceElf:
            race = style->gender % 2 + RaceElfMale;
            break;
        case CharacterRaceChoiceDwarf:
            race = style->gender % 2 + RaceDwarfMale;
            break;
        case CharacterRaceChoiceOrc:
            race = style->gender % 2 + RaceOrcMale;
            break;
        case CharacterRaceChoiceGoblin:
            race = style->gender % 2 + RaceGoblinMale;
            break;
        case CharacterRaceChoiceLizard:
            race = style->gender % 2 + RaceLizardmanMale;
            break;
        case CharacterRaceChoiceUndead:
            race = style->gender % 2 + RaceUndeadMale;
            break;
        case CharacterRaceChoiceFrogman:
            race = style->gender % 2 + RaceFrogmanMale;
            break;
        }
    }

    appearance.size = plasma::Vector<3, float>(0.8f, 0.8f, 1.8f) * 1.2f;

    switch (race) {
    case RaceElfMale:
        appearance.hairColor = style->hairColor;
        appearance.headModel = style->face % ElfHeadM01VariantCount + SpriteElfHeadM01;
        appearance.hairModel = style->haircut % ElfHairM01VariantCount + SpriteElfHairM01;
        appearance.chestModel = SpriteBody4Slot1;
        appearance.feetModel = SpriteFoot;
        appearance.handsModel = SpriteHand2;
        break;
    case RaceHumanMale:
        appearance.hairColor = style->hairColor;
        appearance.headModel = style->face % HumanHeadM01VariantCount + SpriteHumanHeadM01;
        appearance.hairModel = style->haircut % HumanHairM01VariantCount + SpriteHumanHairM01;
        appearance.chestModel = SpriteBody4Slot1;
        appearance.feetModel = SpriteFoot;
        appearance.handsModel = appearance.headModel > SpriteHumanHeadM03 ? SpriteBrownHand : SpriteHand2;
        break;
    case RaceHumanFemale:
        appearance.hairColor = style->hairColor;
        appearance.headModel = style->face % HumanHeadF01VariantCount + SpriteHumanHeadF01;
        appearance.hairModel = style->haircut % HumanHairF01VariantCount + SpriteHumanHairF01;
        appearance.chestModel = SpriteBody4Slot1;
        appearance.feetModel = SpriteFoot;
        appearance.handsModel = appearance.headModel > SpriteHumanHeadF03 ? SpriteBrownHand : SpriteHand2;
        break;
    case RaceFrogmanMale:
        appearance.hairColor = plasma::Vector<3, unsigned char>(255, 255, 255);
        appearance.headModel = style->face % FrogmanHeadM01VariantCount + SpriteFrogmanHeadM01;
        appearance.hairModel = style->haircut % FrogmanHairM01VariantCount + SpriteFrogmanHairM01;
        appearance.chestModel = SpriteBody4Slot1;
        appearance.feetModel = SpriteFoot;
        appearance.handsModel = SpriteFrogmanHand;
        break;
    case RaceFrogmanFemale:
        appearance.hairColor = plasma::Vector<3, unsigned char>(255, 255, 255);
        appearance.headModel = style->face % FrogmanHeadF01VariantCount + SpriteFrogmanHeadF01;
        appearance.hairModel = style->haircut % FrogmanHairF01VariantCount + SpriteFrogmanHairF01;
        appearance.chestModel = SpriteBody4Slot1;
        appearance.feetModel = SpriteFoot;
        appearance.handsModel = SpriteFrogmanHand;
        break;
    case RaceVampire:
        appearance.headModel = SpriteVampireHead;
        appearance.hairModel = SpriteVampireEyes;
        appearance.chestModel = SpriteWoolChest5;
        appearance.feetModel = SpriteFoot;
        appearance.handsModel = SpriteVampireHand;
        appearance.wingsModel = SpriteBatHand;
        appearance.wingScale = 2.0f;
        appearance.wingsRotation = 60.0f;
        appearance.appearanceFlags |= ((1 << AppearanceFlagFlying) | (1 << AppearanceFlagSuppressHairDebris));
        break;
    case RaceElfFemale:
        appearance.hairColor = style->hairColor;
        appearance.headModel = style->face % ElfHeadF01VariantCount + SpriteElfHeadF01;
        appearance.hairModel = style->haircut % ElfHairF01VariantCount + SpriteElfHairF01;
        appearance.chestModel = SpriteBody2Slot0;
        appearance.feetModel = SpriteFoot;
        appearance.handsModel = SpriteHand2;
        break;
    case RaceBandit:
        appearance.headModel = SpriteElfHeadFemale;
        appearance.hairModel = SpriteNone;
        appearance.chestModel = SpriteBody2Slot0;
        appearance.feetModel = SpriteFoot;
        appearance.handsModel = SpriteHand2;
        break;
    case RaceOldMan:
        appearance.headModel = SpriteOldHead;
        appearance.chestModel = SpriteBody2Slot0;
        appearance.feetModel = SpriteFoot;
        appearance.handsModel = SpriteHand2;
        break;
    case RaceGoblinMale:
        appearance.hairColor = style->hairColor;
        appearance.headModel = style->face % GoblinHeadM01VariantCount + SpriteGoblinHeadM01;
        appearance.hairModel = style->haircut % GoblinHairM01VariantCount + SpriteGoblinHairM01;
        appearance.chestModel = SpriteBody2Slot0;
        appearance.feetModel = SpriteFoot;
        appearance.handsModel = SpriteGoblinHand;
        appearance.weaponScale = 1.2f;
        appearance.size /= 1.2f;
        break;
    case RaceGoblinFemale:
        appearance.hairColor = style->hairColor;
        appearance.headModel = style->face % GoblinHeadF01VariantCount + SpriteGoblinHeadF01;
        appearance.hairModel = style->haircut % GoblinHairF01VariantCount + SpriteGoblinHairF01;
        appearance.chestModel = SpriteBody2Slot0;
        appearance.feetModel = SpriteFoot;
        appearance.handsModel = SpriteGoblinHand;
        appearance.weaponScale = 1.2f;
        appearance.size /= 1.2f;
        break;
    case RaceBullterrier: {
        int blue = rand() % 256;
        int green = rand() % 256;
        int red = rand() % 256;
        appearance.hairColor = plasma::Vector<3, unsigned char>(red, green, blue);
        appearance.headModel = rand() % BullterrierHead1VariantCount + SpriteBullterrierHead1;
        appearance.chestModel = SpriteBody2Slot0;
        appearance.feetModel = SpriteFoot;
        appearance.handsModel = SpriteBrownHand;
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 3.0f, 5.0f);
        appearance.headScale = 0.9f;
        appearance.weaponScale = 1.2f;
        break;
    }
    case RaceLizardmanMale:
        appearance.hairColor = style->hairColor;
        appearance.headModel = style->face % LizardHeadM01VariantCount + SpriteLizardHeadM01;
        appearance.hairModel = style->haircut % LizardHairM01VariantCount + SpriteLizardHairM01;
        appearance.chestModel = SpriteLizardBody;
        appearance.feetModel = SpriteLizardFoot;
        appearance.handsModel = SpriteLizardHand;
        break;
    case RaceLizardmanFemale:
        appearance.hairColor = style->hairColor;
        appearance.headModel = style->face % LizardHeadF01VariantCount + SpriteLizardHeadF01;
        appearance.hairModel = style->haircut % LizardHairM01VariantCount + SpriteLizardHairM01;
        appearance.chestModel = SpriteLizardBody;
        appearance.feetModel = SpriteLizardFoot;
        appearance.handsModel = SpriteLizardHand;
        break;
    case RaceGnobold:
        appearance.headModel = SpriteGnoboldHead;
        appearance.chestModel = SpriteZombieBody;
        appearance.feetModel = SpriteGnoboldFoot;
        appearance.handsModel = SpriteGnoboldHand;
        appearance.appearanceFlags |= (1 << AppearanceFlagUnarmored);
        break;
    case RaceInsectoid:
        appearance.headModel = SpriteInsectoidHead;
        appearance.chestModel = SpriteInsectoidBody;
        appearance.feetModel = SpriteInsectoidFoot;
        appearance.handsModel = SpriteInsectoidHand;
        appearance.handsPosition = plasma::Vector<3, float>(6.0f, 0.0f, 1.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, -4.0f);
        appearance.appearanceFlags |= (1 << AppearanceFlagUnarmored);
        break;
    case RaceGnoll:
        appearance.headModel = SpriteGnollHead;
        appearance.chestModel = SpriteGnollBody;
        appearance.feetModel = SpriteGnollFoot;
        appearance.handsModel = SpriteGnollHand;
        appearance.size /= 1.2f;
        appearance.appearanceFlags |= (1 << AppearanceFlagUnarmored);
        break;
    case RacePolarGnoll:
        appearance.headModel = SpritePolarGnollHead;
        appearance.chestModel = SpritePolarGnollBody;
        appearance.feetModel = SpritePolarGnollFoot;
        appearance.handsModel = SpritePolarGnollHand;
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 1.5f, 4.0f);
        appearance.headScale = 1.2f;
        appearance.size /= 1.2f;
        appearance.appearanceFlags |= (1 << AppearanceFlagUnarmored);
        break;
    case RaceMermaid:
        appearance.hairColor = style->hairColor;
        appearance.headModel = style->face % MermaidHeadF01VariantCount + SpriteMermaidHeadF01;
        appearance.hairModel = style->haircut % MermaidHairF01VariantCount + SpriteMermaidHairF01;
        appearance.chestModel = SpriteMermaidBody;
        appearance.handsModel = SpriteMermaidHand;
        break;
    case RaceMerman:
        appearance.hairColor = style->hairColor;
        appearance.headModel = style->face % MermanHeadM01VariantCount + SpriteMermanHeadM01;
        appearance.hairModel = style->haircut % MermanHairM01VariantCount + SpriteMermanHairM01;
        appearance.chestModel = SpriteMermanBody;
        appearance.handsModel = SpriteMermaidHand;
        break;
    case RaceMonkey:
        appearance.headModel = SpriteMonkeyHead;
        appearance.chestModel = SpriteMonkeyBody;
        appearance.feetModel = SpriteMonkeyFoot;
        appearance.handsModel = SpriteMonkeyHand;
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 3.5f, 2.0f);
        appearance.feetPosition = plasma::Vector<3, float>(3.5f, 1.0f, -8.5f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, -4.0f);
        appearance.headScale = 1.05f;
        appearance.chestScale = 0.8f;
        appearance.size = plasma::Vector<3, float>(0.64000005f, 0.64000005f, 1.36f);
        appearance.appearanceFlags |= ((1 << AppearanceFlagHandAttacks) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceDwarfMale:
        appearance.hairColor = style->hairColor;
        appearance.headModel = style->face % DwarfHeadM01VariantCount + SpriteDwarfHeadM01;
        appearance.hairModel = style->haircut % DwarfHairM01VariantCount + SpriteDwarfHairM01;
        appearance.chestModel = SpriteDwarfBody;
        appearance.feetModel = SpriteFoot;
        appearance.handsModel = SpriteHand2;
        appearance.weaponScale = 1.2f;
        appearance.headScale = 0.9f;
        appearance.size /= 1.2f;
        break;
    case RaceDwarfFemale:
        appearance.hairColor = style->hairColor;
        appearance.headModel = style->face % DwarfHeadF01VariantCount + SpriteDwarfHeadF01;
        appearance.hairModel = style->haircut % DwarfHairF01VariantCount + SpriteDwarfHairF01;
        appearance.chestModel = SpriteDwarfBodyFemale;
        appearance.feetModel = SpriteFoot;
        appearance.handsModel = SpriteHand2;
        appearance.weaponScale = 1.2f;
        appearance.headScale = 0.9f;
        appearance.size /= 1.2f;
        break;
    case RaceOrcMale:
        appearance.hairColor = style->hairColor;
        appearance.headModel = style->face % OrcHeadM01VariantCount + SpriteOrcHeadM01;
        appearance.hairModel = style->haircut % OrcHairM01VariantCount + SpriteOrcHairM01;
        appearance.chestModel = SpriteBody2Slot0;
        appearance.feetModel = SpriteFoot;
        appearance.handsModel = SpriteOrcHand;
        appearance.shoulderArmorScale = 1.2f;
        appearance.headScale = 0.9f;
        appearance.size = plasma::Vector<3, float>(1.04f, 1.04f, 2.34f);
        break;
    case RaceOrcFemale:
        appearance.hairColor = style->hairColor;
        appearance.headModel = style->face % OrcHeadF01VariantCount + SpriteOrcHeadF01;
        appearance.hairModel = style->haircut % OrcHairF01VariantCount + SpriteOrcHairF01;
        appearance.chestModel = SpriteBody2Slot0;
        appearance.feetModel = SpriteFoot;
        appearance.handsModel = SpriteOrcHand;
        appearance.shoulderArmorScale = 1.1f;
        appearance.headScale = 0.8f;
        appearance.chestScale = 0.95f;
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 1.5f, 4.0f);
        appearance.size = plasma::Vector<3, float>(1.04f, 1.04f, 2.34f);
        break;
    case RaceTroll:
        appearance.headModel = SpriteTrollHead;
        appearance.chestModel = SpriteTrollBody;
        appearance.feetModel = SpriteTrollFoot;
        appearance.handsModel = SpriteTrollHand;
        appearance.shoulderModel = SpriteTrollArm;
        appearance.shoulderArmorScale = 1.2f;
        appearance.headScale = 0.6f;
        appearance.feetScale = 0.75f;
        appearance.shoulderModelScale = 0.9f;
        appearance.handsPosition = plasma::Vector<3, float>(8.0f, 3.0f, 0.0f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 3.0f, 3.0f);
        appearance.size = plasma::Vector<3, float>(5.6f, 5.6f, 12.599999f);
        appearance.appearanceFlags |= (1 << AppearanceFlagUnarmored);
        break;
    case RaceHellDemon:
        appearance.headModel = SpriteHellDemonHead;
        appearance.chestModel = SpriteHellDemonBody;
        appearance.feetModel = SpriteHellDemonFoot;
        appearance.handsModel = SpriteHellDemonHand;
        appearance.shoulderModel = SpriteHellDemonArm;
        appearance.shoulderArmorScale = 1.1f;
        appearance.headScale = 0.6f;
        appearance.feetScale = 0.75f;
        appearance.shoulderModelScale = 0.85f;
        appearance.handsPosition = plasma::Vector<3, float>(8.0f, 3.0f, 0.0f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 3.0f, 1.0f);
        appearance.size = plasma::Vector<3, float>(3.2f, 3.2f, 7.2f);
        appearance.appearanceFlags |= (1 << AppearanceFlagUnarmored);
        break;
    case RaceGolem:
        appearance.headModel = SpriteGolemHead;
        appearance.chestModel = SpriteGolemBody;
        appearance.feetModel = SpriteGolemFoot;
        appearance.handsModel = SpriteGolemHand;
        appearance.shoulderModel = SpriteGolemArm;
        appearance.shoulderArmorScale = 1.2f;
        appearance.headScale = 0.6f;
        appearance.feetScale = 0.755f;
        appearance.shoulderModelScale = 0.9f;
        appearance.handsPosition = plasma::Vector<3, float>(8.0f, 3.0f, 0.0f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 4.0f, 3.0f);
        appearance.size = plasma::Vector<3, float>(5.6f, 5.6f, 12.599999f);
        appearance.appearanceFlags |= (1 << AppearanceFlagUnarmored);
        break;
    case RaceEmberGolem:
        appearance.headModel = SpriteGolemEmberHead;
        appearance.chestModel = SpriteGolemEmberBody;
        appearance.feetModel = SpriteGolemEmberFoot;
        appearance.handsModel = SpriteGolemEmberHand;
        appearance.shoulderModel = SpriteGolemEmberArm;
        appearance.shoulderArmorScale = 1.2f;
        appearance.headScale = 0.61f;
        appearance.feetScale = 0.755f;
        appearance.shoulderModelScale = 0.9f;
        appearance.handsPosition = plasma::Vector<3, float>(8.0f, 3.0f, 0.0f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 4.0f, 3.0f);
        appearance.size = plasma::Vector<3, float>(5.6f, 5.6f, 12.599999f);
        appearance.appearanceFlags |= (1 << AppearanceFlagUnarmored);
        break;
    case RaceSnowGolem:
        appearance.headModel = SpriteGolemSnowHead;
        appearance.chestModel = SpriteGolemSnowBody;
        appearance.feetModel = SpriteGolemSnowFoot;
        appearance.handsModel = SpriteGolemSnowHand;
        appearance.shoulderModel = SpriteGolemSnowArm;
        appearance.shoulderArmorScale = 1.2f;
        appearance.headScale = 0.61f;
        appearance.feetScale = 0.755f;
        appearance.shoulderModelScale = 0.9f;
        appearance.handsPosition = plasma::Vector<3, float>(8.0f, 3.0f, 0.0f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 4.0f, 3.0f);
        appearance.size = plasma::Vector<3, float>(5.6f, 5.6f, 12.599999f);
        appearance.appearanceFlags |= (1 << AppearanceFlagUnarmored);
        break;
    case RaceWerewolf:
        appearance.headModel = SpriteWerewolfHead;
        appearance.chestModel = SpriteWerewolfBody;
        appearance.feetModel = SpriteWerewolfFoot;
        appearance.handsModel = SpriteWerewolfHand;
        appearance.shoulderArmorScale = 1.2f;
        appearance.headScale = 0.8f;
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 3.0f, 3.0f);
        appearance.handsPosition = plasma::Vector<3, float>(5.0f, 4.0f, 0.0f);
        appearance.handsRotation = plasma::Vector<3, float>(0.0f, -45.0f, 0.0f);
        appearance.handsScale = 0.6f;
        appearance.size = plasma::Vector<3, float>(0.8f, 0.8f, 1.8f) * 3.0f;
        appearance.appearanceFlags |= ((1 << AppearanceFlagHandAttacks) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceDarkTroll:
        appearance.headModel = SpriteDarkTrollHead;
        appearance.hairModel = SpriteDarkTrollEyes;
        appearance.chestModel = SpriteDarkTrollBody;
        appearance.feetModel = SpriteDarkTrollFoot;
        appearance.handsModel = SpriteDarkTrollHand;
        appearance.shoulderModel = SpriteDarkTrollArm;
        appearance.shoulderArmorScale = 1.2f;
        appearance.headScale = 0.6f;
        appearance.feetScale = 0.75f;
        appearance.shoulderModelScale = 0.9f;
        appearance.handsPosition = plasma::Vector<3, float>(8.0f, 3.0f, 0.0f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 3.0f, 3.0f);
        appearance.size = plasma::Vector<3, float>(5.6f, 5.6f, 12.599999f);
        appearance.appearanceFlags |= ((1 << AppearanceFlagUnarmored) | (1 << AppearanceFlagSuppressHairDebris));
        break;
    case RaceYeti:
        appearance.headModel = SpriteYetiHead;
        appearance.chestModel = SpriteYetiBody;
        appearance.feetModel = SpriteYetiFoot;
        appearance.handsModel = SpriteYetiHand;
        appearance.shoulderModel = SpriteYetiArm;
        appearance.shoulderArmorScale = 1.2f;
        appearance.headScale = 0.6f;
        appearance.feetScale = 0.75f;
        appearance.shoulderModelScale = 0.9f;
        appearance.handsPosition = plasma::Vector<3, float>(8.0f, 3.0f, 0.0f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 3.0f, 3.0f);
        appearance.size = plasma::Vector<3, float>(5.6f, 5.6f, 12.599999f);
        appearance.appearanceFlags |= (1 << AppearanceFlagUnarmored);
        break;
    case RaceCyclops:
        appearance.headModel = SpriteCyclopsHead;
        appearance.chestModel = SpriteCyclopsBody;
        appearance.feetModel = SpriteCyclopsFoot;
        appearance.handsModel = SpriteCyclopsHand;
        appearance.shoulderModel = SpriteCyclopsArm;
        appearance.shoulderArmorScale = 1.2f;
        appearance.headScale = 0.6f;
        appearance.feetScale = 0.75f;
        appearance.shoulderModelScale = 0.9f;
        appearance.handsPosition = plasma::Vector<3, float>(8.0f, 3.0f, 0.0f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 3.0f, 3.0f);
        appearance.size = plasma::Vector<3, float>(4.8f, 4.8f, 10.799999f);
        appearance.appearanceFlags |= (1 << AppearanceFlagUnarmored);
        break;
    case RaceRuneGiant:
        appearance.headModel = SpriteRuneGiantHeadLaser;
        appearance.chestModel = rand() % RuneGiantBody01VariantCount + SpriteRuneGiantBody01;
        appearance.feetModel = SpriteRuneGiantFoot01;
        appearance.handsModel = SpriteRuneGiantHand01;
        appearance.shoulderArmorScale = 1.2f;
        appearance.headScale = 0.7f;
        appearance.shoulderModelScale = 0.9f;
        appearance.handsPosition = plasma::Vector<3, float>(8.0f, 0.0f, 5.0f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 4.0f, 8.0f);
        appearance.feetScale = 0.8f;
        appearance.handsScale = 1.5f;
        appearance.feetPosition = plasma::Vector<3, float>(3.0f, 1.0f, -9.5f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 2.0f);
        appearance.size = plasma::Vector<3, float>(4.0f, 4.0f, 10.0f);
        appearance.appearanceFlags |=
            ((1 << AppearanceFlagHeadAimOrigin) | (1 << AppearanceFlagHandAttacks) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceUndeadMale:
        appearance.hairColor = style->hairColor;
        appearance.headModel = style->face % UndeadHeadM01VariantCount + SpriteUndeadHeadM01;
        appearance.hairModel = style->haircut % UndeadHairM01VariantCount + SpriteUndeadHairM01;
        appearance.chestModel = SpriteBody2Slot0;
        appearance.feetModel = SpriteFoot;
        appearance.handsModel = SpriteUndeadHand;
        appearance.headScale = 0.9f;
        break;
    case RaceUndeadFemale:
        appearance.hairColor = style->hairColor;
        appearance.headModel = style->face % UndeadHeadF01VariantCount + SpriteUndeadHeadF01;
        appearance.hairModel = style->haircut % UndeadHairF01VariantCount + SpriteUndeadHairF01;
        appearance.chestModel = SpriteBody2Slot0;
        appearance.feetModel = SpriteFoot;
        appearance.handsModel = SpriteUndeadHand;
        appearance.headScale = 0.9f;
        break;
    case RaceSkeleton:
        appearance.headModel = SpriteSkeletonHead;
        appearance.hairModel = SpriteSkeletonEyes;
        appearance.chestModel = SpriteSkeletonBody;
        appearance.feetModel = SpriteSkeletonFoot;
        appearance.handsModel = SpriteSkeletonHand;
        appearance.headScale = 0.8f;
        appearance.handsRotation = plasma::Vector<3, float>(0.0f, -90.0f, 0.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, -2.0f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 4.0f, 7.0f);
        appearance.handsPosition = plasma::Vector<3, float>(5.0f, 3.0f, 0.0f);
        appearance.appearanceFlags |= ((1 << AppearanceFlagHandAttacks) | (1 << AppearanceFlagUnarmored) |
                                       (1 << AppearanceFlagSuppressHairDebris));
        break;
    case RaceSanta:
        appearance.headModel = SpriteSantaHead;
        appearance.chestModel = SpriteSantaBody;
        appearance.feetModel = SpriteFoot;
        appearance.handsModel = SpriteHand2;
        appearance.headScale = 0.8f;
        appearance.appearanceFlags |= (1 << AppearanceFlagUnarmored);
        break;
    case RaceWizard:
        appearance.headModel = SpriteWizardHead;
        appearance.chestModel = SpriteWizardBody;
        appearance.feetModel = SpriteFoot;
        appearance.handsModel = SpriteHand2;
        appearance.appearanceFlags |= (1 << AppearanceFlagUnarmored);
        break;
    case RaceWitch:
        appearance.headModel = SpriteWitchHead;
        appearance.chestModel = SpriteWitchBody;
        appearance.feetModel = SpriteFoot;
        appearance.handsModel = SpriteHand2;
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 3.0f, 4.0f);
        appearance.headScale = 0.8f;
        appearance.appearanceFlags |= (1 << AppearanceFlagUnarmored);
        break;
    case RaceZombie:
        appearance.headModel = SpriteZombieHead;
        appearance.hairModel = SpriteNone;
        appearance.chestModel = SpriteZombieBody;
        appearance.feetModel = SpriteZombieFoot;
        appearance.handsModel = SpriteZombieHand;
        appearance.handsPosition = plasma::Vector<3, float>(6.0f, 3.0f, -2.0f);
        appearance.handsRotation = plasma::Vector<3, float>(0.0f, -90.0f, 0.0f);
        appearance.appearanceFlags |= ((1 << AppearanceFlagHandAttacks) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceSpikeCreature:
        appearance.headModel = SpriteSpikeHead;
        appearance.chestModel = SpriteSpikeBody;
        appearance.feetModel = SpriteSpikeFoot;
        appearance.handsModel = SpriteSpikeHand;
        appearance.handsPosition = plasma::Vector<3, float>(6.0f, 0.0f, -2.0f);
        appearance.chestScale = 0.9f;
        appearance.appearanceFlags |= (1 << AppearanceFlagUnarmored);
        break;
    case RaceAnubis:
        appearance.headModel = SpriteAnubisHead;
        appearance.chestModel = SpriteAnubisBody;
        appearance.feetModel = SpriteAnubisFoot;
        appearance.handsModel = SpriteAnubisHand;
        appearance.handsPosition = plasma::Vector<3, float>(6.0f, 0.0f, -2.0f);
        appearance.chestScale = 0.9f;
        appearance.appearanceFlags |= (1 << AppearanceFlagUnarmored);
        break;
    case RaceHorus:
        appearance.headModel = SpriteHorusHead;
        appearance.chestModel = SpriteHorusBody;
        appearance.feetModel = SpriteHorusFoot;
        appearance.handsModel = SpriteHorusHand;
        appearance.handsPosition = plasma::Vector<3, float>(6.0f, 0.0f, -2.0f);
        appearance.chestScale = 0.9f;
        appearance.appearanceFlags |= (1 << AppearanceFlagUnarmored);
        break;
    case RaceJester:
        appearance.headModel = SpriteJesterHead;
        appearance.chestModel = SpriteJesterBody;
        appearance.feetModel = SpriteJesterFoot;
        appearance.handsModel = SpriteJesterHand;
        appearance.handsPosition = plasma::Vector<3, float>(6.0f, 0.0f, -2.0f);
        appearance.chestScale = 0.9f;
        appearance.appearanceFlags |= (1 << AppearanceFlagUnarmored);
        break;
    case RaceSpectrino:
        appearance.headModel = SpriteSpectrinoHead;
        appearance.chestModel = SpriteSpectrinoBody;
        appearance.feetModel = SpriteSpectrinoFoot;
        appearance.handsModel = SpriteSpectrinoHand;
        appearance.appearanceFlags |= (1 << AppearanceFlagUnarmored);
        break;
    case RaceNomadMale:
        appearance.headModel = rand() % DesertNomadHead1VariantCount + SpriteDesertNomadHead1;
        appearance.chestModel = SpriteDesertNomadBody;
        appearance.feetModel = SpriteDesertNomadFoot;
        appearance.handsModel =
            appearance.headModel != SpriteDesertNomadHead1 ? SpriteDesertNomadHand2 : SpriteDesertNomadHand;
        appearance.headScale = 0.9f;
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 0.8f, 6.0f);
        appearance.appearanceFlags |= (1 << AppearanceFlagUnarmored);
        break;
    case RaceNomadFemale:
        appearance.headModel = rand() % DesertNomadFemaleHead1VariantCount + SpriteDesertNomadFemaleHead1;
        appearance.chestModel = SpriteDesertNomadBody;
        appearance.feetModel = SpriteDesertNomadFoot;
        appearance.handsModel =
            appearance.headModel != SpriteDesertNomadFemaleHead1 ? SpriteDesertNomadHand2 : SpriteDesertNomadHand;
        appearance.headScale = 0.9f;
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 0.8f, 6.0f);
        appearance.appearanceFlags |= (1 << AppearanceFlagUnarmored);
        break;
    case RaceDjinn:
        appearance.headModel = SpriteDjinnHead;
        appearance.chestModel = SpriteDjinnBody;
        appearance.feetModel = SpriteDjinnFoot;
        appearance.handsModel = SpriteDjinnHand;
        appearance.headScale = 0.8f;
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 1.8f, 6.0f);
        appearance.feetScale = 0.8f;
        appearance.feetPosition = plasma::Vector<3, float>(3.5f, 1.0f, -10.8f);
        appearance.size *= 1.5f;
        appearance.appearanceFlags |= (1 << AppearanceFlagUnarmored);
        break;
    case RaceMinotaur:
        appearance.headModel = SpriteMinotaurHead;
        appearance.chestModel = SpriteMinotaurBody;
        appearance.feetModel = SpriteMinotaurFoot;
        appearance.handsModel = SpriteMinotaurHand;
        appearance.shoulderModel = SpriteMinotaurArm;
        appearance.shoulderModelScale = 0.65f;
        appearance.headScale = 0.5f;
        appearance.feetScale = 0.9f;
        appearance.handsScale = 0.7f;
        appearance.chestScale = 0.9f;
        appearance.weaponScale = 0.9f;
        appearance.handsPosition = plasma::Vector<3, float>(7.0f, 0.0f, 1.5f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 3.0f, 4.5f);
        appearance.feetPosition = plasma::Vector<3, float>(3.0f, 1.0f, -7.5f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, -2.0f);
        appearance.size = plasma::Vector<3, float>(3.2f, 3.2f, 5.8f);
        appearance.appearanceFlags |= (1 << AppearanceFlagUnarmored);
        break;
    case RaceImp:
        appearance.headModel = SpriteImpHead;
        appearance.hairModel = SpriteImpEyes;
        appearance.chestModel = SpriteImpBody;
        appearance.feetModel = SpriteImpFoot;
        appearance.handsModel = SpriteImpHand;
        appearance.handsPosition = plasma::Vector<3, float>(8.0f, 5.0f, 0.0f);
        appearance.chestScale = 0.9f;
        appearance.handsRotation[0] = -90.0f;
        appearance.handsRotation[1] = 30.0f;
        appearance.appearanceFlags |= ((1 << AppearanceFlagHandAttacks) | (1 << AppearanceFlagUnarmored) |
                                       (1 << AppearanceFlagSuppressHairDebris));
        break;
    case RaceFrog:
        appearance.headModel = SpriteFrogHead;
        appearance.chestModel = SpriteFrogBody;
        appearance.feetModel = SpriteFrogFoot;
        appearance.handsModel = SpriteFrogHand;
        appearance.handsPosition = plasma::Vector<3, float>(8.0f, 0.0f, 2.0f);
        appearance.feetPosition = plasma::Vector<3, float>(5.0f, 1.0f, -8.5f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 1.0f, 2.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, -6.0f);
        appearance.handsScale = 1.2f;
        appearance.appearanceFlags |= (1 << AppearanceFlagUnarmored);
        break;
    case RacePlantCreature:
        appearance.headModel = SpritePlantCreatureHead;
        appearance.chestModel = SpritePlantCreatureBody;
        appearance.feetModel = SpritePlantCreatureFoot;
        appearance.handsModel = SpritePlantCreatureHand;
        appearance.handsPosition = plasma::Vector<3, float>(6.0f, 3.0f, 0.0f);
        appearance.feetPosition = plasma::Vector<3, float>(3.0f, 2.0f, -10.0f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 1.0f, 7.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, -4.0f);
        appearance.handsScale = 0.8f;
        appearance.feetScale = 0.7f;
        appearance.size = plasma::Vector<3, float>(0.8f, 0.8f, 1.8f) * 1.5f;
        appearance.handsRotation = plasma::Vector<3, float>(-45.0f, 45.0f, -45.0f);
        appearance.appearanceFlags |= ((1 << AppearanceFlagHandAttacks) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceRadishCreature:
        appearance.chestModel = SpriteRadishCreatureHead;
        appearance.feetModel = SpriteRadishCreatureFoot;
        appearance.handsModel = SpriteRadishCreatureHand;
        appearance.handsPosition = plasma::Vector<3, float>(6.0f, 3.0f, 0.0f);
        appearance.feetPosition = plasma::Vector<3, float>(3.0f, 2.0f, -8.0f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 1.0f, 7.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 5.0f);
        appearance.size = plasma::Vector<3, float>(0.8f, 0.8f, 1.8f) * 1.5f;
        appearance.handsRotation = plasma::Vector<3, float>(-45.0f, 45.0f, -45.0f);
        appearance.appearanceFlags |= ((1 << AppearanceFlagHandAttacks) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceOnionling:
        appearance.chestModel = SpriteOnionlingHead;
        appearance.feetModel = SpriteOnionlingFoot;
        appearance.feetPosition = plasma::Vector<3, float>(3.0f, 0.0f, -12.0f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 1.0f);
        appearance.size = plasma::Vector<3, float>(0.8f, 0.8f, 1.8f) * 1.5f;
        appearance.appearanceFlags |= ((1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceDesertOnionling:
        appearance.chestModel = SpriteDesertOnionling;
        appearance.feetModel = SpriteDesertOnionlingFoot;
        appearance.feetPosition = plasma::Vector<3, float>(3.0f, 0.0f, -12.0f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 1.0f);
        appearance.size = plasma::Vector<3, float>(0.8f, 0.8f, 1.8f) * 1.5f;
        appearance.appearanceFlags |= ((1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceDevourer:
        appearance.chestModel = SpriteDevourerHead;
        appearance.feetModel = SpriteDevourerFoot;
        appearance.feetPosition = plasma::Vector<3, float>(3.0f, 0.0f, -11.0f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 0.0f, -12.0f);
        appearance.size = plasma::Vector<3, float>(0.8f, 0.8f, 1.8f) * 1.5f;
        appearance.appearanceFlags |= ((1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceDuckbill:
        appearance.headModel = SpriteDuckbillHead;
        appearance.chestModel = SpriteDuckbillBody;
        appearance.feetModel = SpriteDuckbillFoot;
        appearance.handsModel = SpriteDuckbillFoot;
        appearance.tailModel = SpriteDuckbillTail;
        appearance.size = plasma::Vector<3, float>(1.0f, 1.0f, 1.0f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 8.5f, 1.5f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 0.6f);
        appearance.handsPosition = plasma::Vector<3, float>(4.0f, 1.0f, -3.7f);
        appearance.feetPosition = plasma::Vector<3, float>(4.0f, -3.0f, -3.7f);
        appearance.chestScale = 0.9f;
        appearance.headScale = 0.73f;
        appearance.feetScale = 1.0f;
        appearance.handsScale = 1.0f;
        appearance.tailPosition = plasma::Vector<3, float>(0.0f, -8.5f, -2.9f);
        appearance.tailScale = 0.8f;
        appearance.tailRotation = -80.0f;
        appearance.appearanceFlags |=
            ((1 << AppearanceFlagQuadruped) | (1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceCrocodile:
        appearance.headModel = SpriteCrocodileHead;
        appearance.chestModel = SpriteCrocodileBody;
        appearance.feetModel = SpriteCrocodileFoot;
        appearance.handsModel = SpriteCrocodileFoot;
        appearance.size = plasma::Vector<3, float>(1.0f, 1.0f, 1.0f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 17.5f, 2.5f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, -9.0f, 2.6f);
        appearance.handsPosition = plasma::Vector<3, float>(6.0f, 9.0f, -1.7f);
        appearance.feetPosition = plasma::Vector<3, float>(6.0f, -9.0f, -1.7f);
        appearance.chestScale = 2.0f;
        appearance.headScale = 1.1f;
        appearance.feetScale = 1.6f;
        appearance.handsScale = 1.6f;
        appearance.appearanceFlags |=
            ((1 << AppearanceFlagQuadruped) | (1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceMole:
        appearance.headModel = SpriteMoleHead;
        appearance.chestModel = SpriteMoleBody;
        appearance.feetModel = SpriteMoleFoot;
        appearance.handsModel = SpriteMoleHand;
        appearance.handsPosition = plasma::Vector<3, float>(9.0f, 0.0f, 2.0f);
        appearance.feetPosition = plasma::Vector<3, float>(5.0f, 1.0f, -8.5f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 1.0f, 2.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, -6.0f);
        appearance.handsRotation[1] = -30.0f;
        appearance.handsScale = 0.9f;
        appearance.size /= 1.2f;
        appearance.appearanceFlags |= ((1 << AppearanceFlagHandAttacks) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceBiter:
        appearance.headModel = SpriteBiterHead;
        appearance.chestModel = SpriteBiterBody;
        appearance.feetModel = SpriteBiterFoot;
        appearance.handsModel = SpriteBiterHand;
        appearance.handsPosition = plasma::Vector<3, float>(9.0f, 0.0f, 2.0f);
        appearance.feetPosition = plasma::Vector<3, float>(5.0f, 1.0f, -8.5f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 1.0f, 3.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, -5.0f);
        appearance.handsRotation[1] = -30.0f;
        appearance.handsScale = 0.9f;
        appearance.size *= 1.1f;
        appearance.appearanceFlags |= ((1 << AppearanceFlagHandAttacks) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceKoala:
        appearance.headModel = SpriteKoalaHead;
        appearance.chestModel = SpriteKoalaBody;
        appearance.feetModel = SpriteKoalaFoot;
        appearance.handsModel = SpriteKoalaHand;
        appearance.handsPosition = plasma::Vector<3, float>(9.0f, 0.0f, 2.0f);
        appearance.feetPosition = plasma::Vector<3, float>(5.0f, 1.0f, -8.5f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 1.0f, 3.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, -5.0f);
        appearance.handsRotation[1] = -30.0f;
        appearance.handsScale = 0.9f;
        appearance.headScale = 1.2f;
        appearance.size *= 1.1f;
        appearance.appearanceFlags |= ((1 << AppearanceFlagHandAttacks) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceSquirrel:
        appearance.headModel = SpriteSquirrelHead;
        appearance.chestModel = SpriteSquirrelBody;
        appearance.feetModel = SpriteSquirrelFoot;
        appearance.handsModel = SpriteSquirrelHand;
        appearance.handsPosition = plasma::Vector<3, float>(7.0f, 0.0f, 0.0f);
        appearance.feetPosition = plasma::Vector<3, float>(5.0f, 1.0f, -8.5f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 1.0f, 4.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, -5.0f, -5.0f);
        appearance.handsRotation[1] = -30.0f;
        appearance.handsScale = 0.75f;
        appearance.feetScale = 0.8f;
        appearance.headScale = 1.2f;
        appearance.size *= 0.7f;
        appearance.appearanceFlags |= ((1 << AppearanceFlagHandAttacks) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceRaccoon:
        appearance.headModel = SpriteRaccoonHead;
        appearance.chestModel = SpriteRaccoonBody;
        appearance.feetModel = SpriteRaccoonFoot;
        appearance.handsModel = SpriteRaccoonHand;
        appearance.handsPosition = plasma::Vector<3, float>(7.0f, 0.0f, 0.0f);
        appearance.feetPosition = plasma::Vector<3, float>(5.0f, 1.0f, -8.5f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 1.0f, 4.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, -5.0f, -5.0f);
        appearance.handsRotation[1] = -30.0f;
        appearance.handsScale = 0.75f;
        appearance.feetScale = 0.8f;
        appearance.headScale = 1.2f;
        appearance.size *= 0.9f;
        appearance.appearanceFlags |= ((1 << AppearanceFlagHandAttacks) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceOwl:
        appearance.headModel = SpriteOwlHead;
        appearance.feetModel = SpriteOwlFoot;
        appearance.wingsModel = SpriteOwlHand;
        appearance.handsPosition = plasma::Vector<3, float>(9.0f, 0.0f, 2.0f);
        appearance.feetPosition = plasma::Vector<3, float>(5.0f, 0.0f, -11.5f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 0.0f, -2.0f);
        appearance.handsScale = 0.9f;
        appearance.chestRotation = -10.0f;
        appearance.wingsRotation = 80.0f;
        appearance.wingsPosition = plasma::Vector<3, float>(3.0f, -5.0f, 0.0f);
        appearance.size /= 1.2f;
        appearance.appearanceFlags |=
            ((1 << AppearanceFlagFlying) | (1 << AppearanceFlagHandAttacks) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceSpitter:
        appearance.chestModel = SpriteSquidBody;
        appearance.feetModel = SpriteSquidFoot;
        appearance.feetPosition = plasma::Vector<3, float>(5.0f, 1.0f, -5.5f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 2.0f);
        appearance.size = plasma::Vector<3, float>(0.8f, 0.8f, 1.0f);
        appearance.appearanceFlags |= (1 << AppearanceFlagBiteAttack);
        break;
    case RaceBunny:
        appearance.chestModel = SpriteBunnyBody;
        appearance.feetModel = SpriteBunnyFoot;
        appearance.handsPosition = plasma::Vector<3, float>(9.0f, 0.0f, 8.0f);
        appearance.feetPosition = plasma::Vector<3, float>(5.0f, 1.0f, -2.5f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 1.0f, 8.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 2.0f);
        appearance.handsScale = 0.9f;
        appearance.size = plasma::Vector<3, float>(1.0f, 1.0f, 1.1f);
        appearance.appearanceFlags |= ((1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    case RacePorcupine:
        appearance.chestModel = SpritePorcupineBody;
        appearance.feetModel = SpritePorcupineFoot;
        appearance.handsPosition = plasma::Vector<3, float>(9.0f, 0.0f, 8.0f);
        appearance.feetPosition = plasma::Vector<3, float>(5.0f, 1.0f, -2.5f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 1.0f, 8.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 2.0f);
        appearance.handsScale = 0.9f;
        appearance.size = plasma::Vector<3, float>(1.0f, 1.0f, 1.1f);
        appearance.appearanceFlags |= ((1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceOgre: {
        int blue = rand() % 256;
        int green = rand() % 256;
        int red = rand() % 256;
        appearance.hairColor = plasma::Vector<3, unsigned char>(red, green, blue);
        appearance.headModel = SpriteOgreHead;
        appearance.chestModel = SpriteOgreBody;
        appearance.feetModel = SpriteOgreFoot;
        appearance.handsModel = SpriteOgreHand;
        appearance.headScale = 0.61f;
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 4.0f, 6.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, -2.0f);
        appearance.handsPosition = plasma::Vector<3, float>(7.5f, 0.0f, 3.0f);
        appearance.size = plasma::Vector<3, float>(0.8f, 0.8f, 1.8f) * 3.0f;
        appearance.appearanceFlags |= (1 << AppearanceFlagUnarmored);
        break;
    }
    case RaceRockling:
        appearance.headModel = SpriteRocklingHead;
        appearance.feetModel = SpriteRocklingFoot;
        appearance.handsModel = SpriteRocklingHand;
        appearance.weaponScale = 0.65f;
        appearance.headScale = 0.8f;
        appearance.feetScale = 0.85f;
        appearance.handsScale = 0.85f;
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 0.0f, 2.0f);
        appearance.handsPosition = plasma::Vector<3, float>(7.5f, 0.0f, 5.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, -1.0f);
        appearance.feetPosition = plasma::Vector<3, float>(3.0f, 1.0f, -6.5f);
        appearance.size = plasma::Vector<3, float>(2.4f, 2.4f, 3.6000001f);
        appearance.appearanceFlags |= (1 << AppearanceFlagUnarmored);
        break;
    case RacePenguin:
        appearance.headModel = SpritePenguinHead;
        appearance.chestModel = SpritePenguinBody;
        appearance.feetModel = SpritePenguinFoot;
        appearance.handsModel = SpritePenguinHand;
        appearance.handsPosition = plasma::Vector<3, float>(8.0f, 0.0f, 2.0f);
        appearance.feetPosition = plasma::Vector<3, float>(5.0f, 1.0f, -8.5f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 1.0f, 2.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, -6.0f);
        appearance.handsScale = 0.9f;
        appearance.handsRotation = plasma::Vector<3, float>(-60.0f, -45.0f, 0.0f);
        appearance.size /= 1.2f;
        appearance.appearanceFlags |= ((1 << AppearanceFlagHandAttacks) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceGreenSlime:
        appearance.chestModel = SpriteSlimeGreen;
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
        appearance.chestScale = 1.5f;
        appearance.size = plasma::Vector<3, float>(0.8f, 0.8f, 0.8f);
        appearance.appearanceFlags |=
            ((1 << AppearanceFlagQuadruped) | (1 << AppearanceFlagHandAttacks) | (1 << AppearanceFlagUnarmored));
        break;
    case RacePinkSlime:
        appearance.chestModel = SpriteSlimePink;
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
        appearance.chestScale = 1.5f;
        appearance.size = plasma::Vector<3, float>(0.8f, 0.8f, 0.8f);
        appearance.appearanceFlags |=
            ((1 << AppearanceFlagQuadruped) | (1 << AppearanceFlagHandAttacks) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceYellowSlime:
        appearance.chestModel = SpriteSlimeYellow;
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
        appearance.chestScale = 1.5f;
        appearance.size = plasma::Vector<3, float>(0.8f, 0.8f, 0.8f);
        appearance.appearanceFlags |=
            ((1 << AppearanceFlagQuadruped) | (1 << AppearanceFlagHandAttacks) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceBlueSlime:
        appearance.chestModel = SpriteSlimeBlue;
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
        appearance.chestScale = 1.5f;
        appearance.size = plasma::Vector<3, float>(0.8f, 0.8f, 0.8f);
        appearance.appearanceFlags |=
            ((1 << AppearanceFlagQuadruped) | (1 << AppearanceFlagHandAttacks) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceFrightener:
        appearance.headModel = SpriteFrightenerHead;
        appearance.hairModel = SpriteFrightenerEyes;
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
        appearance.headScale = 0.8f;
        appearance.appearanceFlags |= (1 << AppearanceFlagFlying);
        appearance.size = plasma::Vector<3, float>(1.4f, 1.4f, 2.5f);
        appearance.appearanceFlags |= ((1 << AppearanceFlagQuadruped) | (1 << AppearanceFlagHandAttacks) |
                                       (1 << AppearanceFlagUnarmored) | (1 << AppearanceFlagSuppressHairDebris));
        break;
    case RaceSandHorror:
        appearance.headModel = SpriteSandHorrorHead;
        appearance.handsModel = SpriteSandHorrorHand;
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
        appearance.handsPosition = plasma::Vector<3, float>(6.0f, 0.0f, 2.0f);
        appearance.headScale = 0.8f;
        appearance.handsScale = 0.5f;
        appearance.appearanceFlags |= (1 << AppearanceFlagFlying);
        appearance.size = plasma::Vector<3, float>(1.4f, 1.4f, 2.5f);
        appearance.appearanceFlags |= ((1 << AppearanceFlagHandAttacks) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceSapphireFish:
        appearance.chestModel = SpriteFish1;
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
        appearance.size =
            plasma::Vector<3, float>(1.0f, 1.0f, 1.0f) * ((rand() * 0.5f / float(RAND_MAX) + 1.2f) * 0.8f);
        appearance.chestScale = 0.9f;
        appearance.appearanceFlags |= ((1 << AppearanceFlagQuadruped) | (1 << AppearanceFlagBiteAttack) |
                                       (1 << AppearanceFlagUnarmored) | (1 << AppearanceFlagFish));
        break;
    case RaceLemonFish:
        appearance.chestModel = SpriteFish2;
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
        appearance.size =
            plasma::Vector<3, float>(1.0f, 1.0f, 1.0f) * ((rand() * 0.5f / float(RAND_MAX) + 1.2f) * 0.8f);
        appearance.chestScale = 0.9f;
        appearance.appearanceFlags |= ((1 << AppearanceFlagQuadruped) | (1 << AppearanceFlagBiteAttack) |
                                       (1 << AppearanceFlagUnarmored) | (1 << AppearanceFlagFish));
        break;
    case RaceShark:
        appearance.chestModel = SpriteShark;
        appearance.chestScale = 1.2f;
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
        appearance.size = plasma::Vector<3, float>(2.4f, 2.4f, 6.0f);
        appearance.appearanceFlags |= ((1 << AppearanceFlagQuadruped) | (1 << AppearanceFlagBiteAttack) |
                                       (1 << AppearanceFlagUnarmored) | (1 << AppearanceFlagFish));
        break;
    case RaceLanternFish:
        appearance.headModel = SpriteLanternFish;
        appearance.hairModel = SpriteLanternFishEyes;
        appearance.headScale = 1.2f;
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
        appearance.size = plasma::Vector<3, float>(2.4f, 2.4f, 6.0f);
        appearance.appearanceFlags |=
            ((1 << AppearanceFlagQuadruped) | (1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored) |
             (1 << AppearanceFlagFish) | (1 << AppearanceFlagSuppressHairDebris));
        break;
    case RaceMawFish:
        appearance.chestModel = SpriteMawfish;
        appearance.chestScale = 1.2f;
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
        appearance.size = plasma::Vector<3, float>(2.4f, 2.4f, 6.0f);
        appearance.appearanceFlags |= ((1 << AppearanceFlagQuadruped) | (1 << AppearanceFlagBiteAttack) |
                                       (1 << AppearanceFlagUnarmored) | (1 << AppearanceFlagFish));
        break;
    case RaceBlowfish:
        appearance.chestModel = SpriteBlowfish;
        appearance.chestScale = 1.2f;
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
        appearance.size = plasma::Vector<3, float>(2.4f, 2.4f, 6.0f);
        appearance.appearanceFlags |= ((1 << AppearanceFlagQuadruped) | (1 << AppearanceFlagBiteAttack) |
                                       (1 << AppearanceFlagUnarmored) | (1 << AppearanceFlagFish));
        break;
    case RacePiranha:
        appearance.chestModel = SpritePiranha;
        appearance.chestScale = 1.2f;
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
        appearance.size = plasma::Vector<3, float>(2.4f, 2.4f, 6.0f);
        appearance.appearanceFlags |= ((1 << AppearanceFlagQuadruped) | (1 << AppearanceFlagBiteAttack) |
                                       (1 << AppearanceFlagUnarmored) | (1 << AppearanceFlagFish));
        break;
    case RaceSeahorse:
        appearance.chestModel = SpriteSeahorse;
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
        appearance.size = plasma::Vector<3, float>(0.8f, 0.8f, 2.0f);
        appearance.appearanceFlags |= ((1 << AppearanceFlagQuadruped) | (1 << AppearanceFlagBiteAttack) |
                                       (1 << AppearanceFlagUnarmored) | (1 << AppearanceFlagFish));
        break;
    case RaceCrab:
        appearance.headModel = SpriteCrabHead;
        appearance.chestModel = SpriteCrabBody;
        appearance.feetModel = SpriteCrabFoot;
        appearance.handsModel = SpriteCrabHand;
        appearance.handsPosition = plasma::Vector<3, float>(10.0f, 0.0f, 8.0f);
        appearance.feetPosition = plasma::Vector<3, float>(6.0f, 1.0f, -5.5f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 1.0f, 5.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 1.0f);
        appearance.handsRotation = plasma::Vector<3, float>(90.0f, 30.0f, 0.0f);
        appearance.headScale = 0.75f;
        appearance.handsScale = 1.2f;
        appearance.size = plasma::Vector<3, float>(0.8f, 0.8f, 1.2f);
        appearance.appearanceFlags |= ((1 << AppearanceFlagHandAttacks) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceSeaCrab:
        appearance.headModel = SpriteCrabHeadBlue;
        appearance.chestModel = SpriteCrabBodyBlue;
        appearance.feetModel = SpriteCrabFootBlue;
        appearance.handsModel = SpriteCrabHandBlue;
        appearance.handsPosition = plasma::Vector<3, float>(10.0f, 0.0f, 8.0f);
        appearance.feetPosition = plasma::Vector<3, float>(6.0f, 1.0f, -5.5f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 1.0f, 5.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 1.0f);
        appearance.handsRotation = plasma::Vector<3, float>(90.0f, 30.0f, 0.0f);
        appearance.headScale = 0.75f;
        appearance.handsScale = 1.2f;
        appearance.size = plasma::Vector<3, float>(4.8f, 4.8f, 7.2000003f);
        appearance.appearanceFlags |= ((1 << AppearanceFlagHandAttacks) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceBarkBeetle:
        appearance.headModel = SpriteBarkbeetleHead;
        appearance.feetModel = SpriteBarkbeetleFoot;
        appearance.feetPosition = plasma::Vector<3, float>(3.5f, 1.0f, -2.5f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 1.0f, 1.0f);
        appearance.headScale = 0.75f;
        appearance.size = plasma::Vector<3, float>(1.6f, 1.6f, 1.6f);
        appearance.appearanceFlags |=
            ((1 << AppearanceFlagQuadruped) | (1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceFireBeetle:
        appearance.headModel = SpriteFirebeetleHead;
        appearance.feetModel = SpriteFirebeetleFoot;
        appearance.feetPosition = plasma::Vector<3, float>(3.5f, 1.0f, -2.5f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 1.0f, 1.0f);
        appearance.headScale = 0.9f;
        appearance.size = plasma::Vector<3, float>(1.6f, 1.6f, 1.6f);
        appearance.appearanceFlags |=
            ((1 << AppearanceFlagQuadruped) | (1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceSnoutBeetle:
        appearance.headModel = SpriteSnoutbeetleHead;
        appearance.feetModel = SpriteSnoutbeetleFoot;
        appearance.feetPosition = plasma::Vector<3, float>(3.5f, 1.0f, -2.5f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 1.0f, 1.0f);
        appearance.headScale = 0.9f;
        appearance.size = plasma::Vector<3, float>(1.6f, 1.6f, 1.6f);
        appearance.appearanceFlags |=
            ((1 << AppearanceFlagQuadruped) | (1 << AppearanceFlagHandAttacks) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceLemonBeetle:
        appearance.headModel = SpriteLemonbeetleHead;
        appearance.feetModel = SpriteLemonbeetleFoot;
        appearance.feetPosition = plasma::Vector<3, float>(3.5f, 1.0f, -2.5f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 1.0f, 1.0f);
        appearance.headScale = 0.9f;
        appearance.size = plasma::Vector<3, float>(1.6f, 1.6f, 1.6f);
        appearance.appearanceFlags |=
            ((1 << AppearanceFlagQuadruped) | (1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceLich:
        appearance.headModel = SpriteLichHead;
        appearance.chestModel = SpriteLichBody;
        appearance.feetModel = SpriteNone;
        appearance.handsModel = SpriteLichHand;
        appearance.shoulderModel = SpriteLichArm;
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 3.0f);
        appearance.handsPosition = plasma::Vector<3, float>(6.0f, 5.0f, 6.0f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 1.0f, 11.0f);
        appearance.headScale = 0.4f;
        appearance.handsScale = 0.75f;
        appearance.handsRotation = plasma::Vector<3, float>(0.0f, -90.0f, 0.0f);
        appearance.size = plasma::Vector<3, float>(6.4f, 6.4f, 14.4f);
        appearance.appearanceFlags |= (1 << AppearanceFlagUnarmored);
        break;
    case RaceHorse:
        appearance.headModel = SpritePonyHead;
        appearance.chestModel = SpritePonyBody;
        appearance.feetModel = SpritePonyFoot;
        appearance.handsModel = SpritePonyFoot;
        appearance.tailModel = SpritePonyTail;
        appearance.headScale = 0.8f;
        appearance.size = plasma::Vector<3, float>(1.4399999f, 1.4399999f, 1.6f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 8.0f, 6.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 3.0f);
        appearance.handsPosition = plasma::Vector<3, float>(3.0f, 3.0f, -3.25f);
        appearance.feetPosition = plasma::Vector<3, float>(3.0f, -5.0f, -3.25f);
        appearance.feetScale = 0.8f;
        appearance.handsScale = 0.8f;
        appearance.tailPosition = plasma::Vector<3, float>(0.0f, -7.0f, -4.0f);
        appearance.tailScale = 1.0f;
        appearance.chestScale = 0.9f;
        appearance.appearanceFlags |=
            ((1 << AppearanceFlagQuadruped) | (1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceCamel:
        appearance.headModel = SpriteCamelHead;
        appearance.chestModel = SpriteCamelBody;
        appearance.feetModel = SpriteCamelFoot;
        appearance.handsModel = SpriteCamelFoot;
        appearance.headScale = 0.65f;
        appearance.size = plasma::Vector<3, float>(1.8f, 1.8f, 3.0f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 12.0f, 4.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 1.0f);
        appearance.handsPosition = plasma::Vector<3, float>(3.0f, 3.0f, -5.75f);
        appearance.feetPosition = plasma::Vector<3, float>(3.0f, -4.0f, -5.75f);
        appearance.feetScale = 0.75f;
        appearance.handsScale = 0.8f;
        appearance.chestScale = 0.95f;
        appearance.appearanceFlags |=
            ((1 << AppearanceFlagQuadruped) | (1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceCow:
        appearance.headModel = SpriteCowHead;
        appearance.chestModel = SpriteCowBody;
        appearance.feetModel = SpriteCowFoot;
        appearance.handsModel = SpriteCowFoot;
        appearance.headScale = 0.65f;
        appearance.size = plasma::Vector<3, float>(1.8f, 1.8f, 3.0f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 10.0f, 1.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, -2.0f);
        appearance.handsPosition = plasma::Vector<3, float>(3.0f, 3.0f, -6.75f);
        appearance.feetPosition = plasma::Vector<3, float>(3.0f, -4.0f, -6.75f);
        appearance.feetScale = 0.75f;
        appearance.handsScale = 0.8f;
        appearance.chestScale = 1.2f;
        appearance.appearanceFlags |=
            ((1 << AppearanceFlagQuadruped) | (1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceCollie:
        appearance.headModel = SpriteCollieHead;
        appearance.chestModel = SpriteCollieBody;
        appearance.feetModel = SpriteCollieFoot;
        appearance.handsModel = SpriteCollieHand;
        appearance.tailModel = SpriteCollieTail;
        appearance.headScale = 0.9f;
        appearance.size = plasma::Vector<3, float>(0.8f, 0.8f, 1.0f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 9.0f, 6.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 2.0f);
        appearance.handsPosition = plasma::Vector<3, float>(3.0f, 4.0f, -2.5f);
        appearance.feetPosition = plasma::Vector<3, float>(3.0f, -4.0f, -1.0f);
        appearance.feetScale = 1.0f;
        appearance.handsScale = 1.0f;
        appearance.tailPosition = plasma::Vector<3, float>(0.0f, -8.0f, -1.0f);
        appearance.tailScale = 1.0f;
        appearance.tailRotation = -30.0f;
        appearance.appearanceFlags |=
            ((1 << AppearanceFlagQuadruped) | (1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceShepherdDog:
        appearance.headModel = SpriteShepherdDogHead;
        appearance.chestModel = SpriteShepherdDogBody;
        appearance.feetModel = SpriteShepherdDogFoot;
        appearance.handsModel = SpriteShepherdDogHand;
        appearance.tailModel = SpriteShepherdDogTail;
        appearance.headScale = 0.9f;
        appearance.size = plasma::Vector<3, float>(0.8f, 0.8f, 1.0f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 9.0f, 6.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 2.0f);
        appearance.handsPosition = plasma::Vector<3, float>(3.0f, 4.0f, -2.5f);
        appearance.feetPosition = plasma::Vector<3, float>(3.0f, -4.0f, -1.0f);
        appearance.feetScale = 1.0f;
        appearance.handsScale = 1.0f;
        appearance.tailPosition = plasma::Vector<3, float>(0.0f, -8.0f, -1.0f);
        appearance.tailScale = 1.0f;
        appearance.tailRotation = -30.0f;
        appearance.appearanceFlags |=
            ((1 << AppearanceFlagQuadruped) | (1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceSkullBull:
        appearance.headModel = SpriteSkullBullHead;
        appearance.chestModel = SpriteSkullBullBody;
        appearance.feetModel = SpriteSkullBullFoot;
        appearance.handsModel = SpriteSkullBullHand;
        appearance.tailModel = SpriteSkullBullTail;
        appearance.headScale = 0.7f;
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 12.5f, 3.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 1.0f);
        appearance.handsPosition = plasma::Vector<3, float>(3.0f, 2.5f, -4.5f);
        appearance.feetPosition = plasma::Vector<3, float>(3.0f, -4.0f, -4.0f);
        appearance.feetScale = 0.8f;
        appearance.handsScale = 0.8f;
        appearance.chestScale = 1.2f;
        appearance.tailPosition = plasma::Vector<3, float>(0.0f, -8.0f, -2.5f);
        appearance.tailScale = 1.0f;
        appearance.tailRotation = -30.0f;
        appearance.size = plasma::Vector<3, float>(2.8f, 2.8f, 4.0f);
        appearance.appearanceFlags |=
            ((1 << AppearanceFlagQuadruped) | (1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceAlpaca:
        appearance.headModel = SpriteAlpacaHead;
        appearance.chestModel = SpriteAlpacaBody;
        appearance.feetModel = SpriteAlpacaFoot;
        appearance.handsModel = SpriteAlpacaHand;
        appearance.headScale = 0.8f;
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 9.0f, 5.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, -4.0f);
        appearance.handsPosition = plasma::Vector<3, float>(3.0f, 4.0f, -8.5f);
        appearance.feetPosition = plasma::Vector<3, float>(3.0f, -4.0f, -9.0f);
        appearance.feetScale = 1.0f;
        appearance.handsScale = 1.0f;
        appearance.appearanceFlags |=
            ((1 << AppearanceFlagQuadruped) | (1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceBrownAlpaca:
        appearance.headModel = SpriteAlpacaBrownHead;
        appearance.chestModel = SpriteAlpacaBrownBody;
        appearance.feetModel = SpriteAlpacaBrownFoot;
        appearance.handsModel = SpriteAlpacaBrownHand;
        appearance.headScale = 0.8f;
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 9.0f, 5.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, -4.0f);
        appearance.handsPosition = plasma::Vector<3, float>(3.0f, 4.0f, -8.5f);
        appearance.feetPosition = plasma::Vector<3, float>(3.0f, -4.0f, -9.0f);
        appearance.feetScale = 1.0f;
        appearance.handsScale = 1.0f;
        appearance.appearanceFlags |=
            ((1 << AppearanceFlagQuadruped) | (1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceTurtle:
        appearance.headModel = SpriteTurtleHead;
        appearance.chestModel = SpriteTurtleBody;
        appearance.feetModel = SpriteTurtleFoot;
        appearance.handsModel = SpriteTurtleFoot;
        appearance.headScale = 0.65f;
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 13.0f, 0.5f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 3.5f);
        appearance.handsPosition = plasma::Vector<3, float>(3.0f, 4.0f, -3.5f);
        appearance.feetPosition = plasma::Vector<3, float>(3.0f, -4.0f, -3.5f);
        appearance.size = plasma::Vector<3, float>(0.8f, 0.8f, 1.0f);
        appearance.feetScale = 0.8f;
        appearance.handsScale = 0.8f;
        appearance.tailPosition = plasma::Vector<3, float>(0.0f, -8.0f, 4.5f);
        appearance.tailScale = 1.0f;
        appearance.tailRotation = -30.0f;
        appearance.appearanceFlags |=
            ((1 << AppearanceFlagQuadruped) | (1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceTerrier:
        appearance.headModel = SpriteDogHead2;
        appearance.chestModel = SpriteDogBody2;
        appearance.feetModel = SpriteDogFoot2;
        appearance.handsModel = SpriteDogHand2;
        appearance.tailModel = SpriteDogTail2;
        appearance.headScale = 0.9f;
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 9.0f, -1.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, -5.5f);
        appearance.handsPosition = plasma::Vector<3, float>(3.0f, 4.0f, -8.5f);
        appearance.feetPosition = plasma::Vector<3, float>(3.0f, -4.1f, -8.5f);
        appearance.feetScale = 1.0f;
        appearance.handsScale = 1.0f;
        appearance.chestScale = 1.05f;
        appearance.tailPosition = plasma::Vector<3, float>(0.0f, -10.0f, 3.0f);
        appearance.tailScale = 0.8f;
        appearance.tailRotation = -120.0f;
        appearance.size = plasma::Vector<3, float>(1.04f, 1.04f, 2.34f);
        appearance.appearanceFlags |=
            ((1 << AppearanceFlagQuadruped) | (1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceScottishTerrier:
        appearance.headModel = SpriteScottishTerrierHead;
        appearance.chestModel = SpriteScottishTerrierBody;
        appearance.feetModel = SpriteScottishTerrierFoot;
        appearance.handsModel = SpriteScottishTerrierHand;
        appearance.tailModel = SpriteScottishTerrierTail;
        appearance.headScale = 0.9f;
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 9.0f, 5.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 0.5f);
        appearance.handsPosition = plasma::Vector<3, float>(3.0f, 4.0f, -2.5f);
        appearance.feetPosition = plasma::Vector<3, float>(3.0f, -4.1f, -2.5f);
        appearance.feetScale = 1.0f;
        appearance.handsScale = 1.0f;
        appearance.chestScale = 1.05f;
        appearance.tailPosition = plasma::Vector<3, float>(0.0f, -7.0f, 2.0f);
        appearance.tailScale = 0.8f;
        appearance.tailRotation = -120.0f;
        appearance.size = plasma::Vector<3, float>(0.8f, 0.8f, 0.7f);
        appearance.appearanceFlags |=
            ((1 << AppearanceFlagQuadruped) | (1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceWolf:
        appearance.headModel = SpriteWolfHead;
        appearance.chestModel = SpriteWolfBody;
        appearance.feetModel = SpriteWolfFoot;
        appearance.handsModel = SpriteWolfHand;
        appearance.tailModel = SpriteWolfTail;
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 11.0f, -1.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, -3.5f);
        appearance.handsPosition = plasma::Vector<3, float>(3.0f, 3.0f, -7.5f);
        appearance.feetPosition = plasma::Vector<3, float>(3.0f, -4.1f, -7.5f);
        appearance.tailPosition = plasma::Vector<3, float>(0.0f, -5.5f, -1.0f);
        appearance.size = plasma::Vector<3, float>(1.52f, 1.52f, 3.23f);
        appearance.chestScale = 1.2f;
        appearance.headScale = 0.8f;
        appearance.feetScale = 0.9f;
        appearance.handsScale = 0.9f;
        appearance.appearanceFlags |=
            ((1 << AppearanceFlagQuadruped) | (1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    case RacePanther:
        appearance.headModel = SpritePantherHead;
        appearance.chestModel = SpritePantherBody;
        appearance.feetModel = SpritePantherFoot;
        appearance.handsModel = SpritePantherHand;
        appearance.tailModel = SpritePantherTail;
        appearance.headScale = 0.6f;
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 14.0f, -1.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
        appearance.handsPosition = plasma::Vector<3, float>(3.0f, 5.0f, -8.5f);
        appearance.feetPosition = plasma::Vector<3, float>(3.0f, -6.0f, -9.0f);
        appearance.feetScale = 1.0f;
        appearance.handsScale = 1.1f;
        appearance.tailPosition = plasma::Vector<3, float>(0.0f, -11.0f, -4.0f);
        appearance.tailScale = 1.0f;
        appearance.tailRotation = -30.0f;
        appearance.appearanceFlags |=
            ((1 << AppearanceFlagQuadruped) | (1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceCat:
        appearance.headModel = SpriteCatHead;
        appearance.chestModel = SpriteCatBody;
        appearance.feetModel = SpriteCatFoot;
        appearance.handsModel = SpriteCatHand;
        appearance.tailModel = SpriteCatTail;
        appearance.headScale = 0.9f;
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 9.0f, -1.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, -4.0f);
        appearance.handsPosition = plasma::Vector<3, float>(3.0f, 4.0f, -8.5f);
        appearance.feetPosition = plasma::Vector<3, float>(3.0f, -4.1f, -8.5f);
        appearance.feetScale = 1.0f;
        appearance.handsScale = 1.0f;
        appearance.tailPosition = plasma::Vector<3, float>(0.0f, -8.0f, -1.0f);
        appearance.tailScale = 1.1f;
        appearance.tailRotation = -30.0f;
        appearance.appearanceFlags |=
            ((1 << AppearanceFlagQuadruped) | (1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceBrownCat:
        appearance.headModel = SpriteCatHead2;
        appearance.chestModel = SpriteCatBody2;
        appearance.feetModel = SpriteCatFoot2;
        appearance.handsModel = SpriteCatHand2;
        appearance.tailModel = SpriteCatTail2;
        appearance.headScale = 0.9f;
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 9.0f, -1.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, -4.0f);
        appearance.handsPosition = plasma::Vector<3, float>(3.0f, 4.0f, -8.5f);
        appearance.feetPosition = plasma::Vector<3, float>(3.0f, -4.1f, -8.5f);
        appearance.feetScale = 1.0f;
        appearance.handsScale = 1.0f;
        appearance.tailPosition = plasma::Vector<3, float>(0.0f, -8.0f, -1.0f);
        appearance.tailScale = 1.1f;
        appearance.tailRotation = -30.0f;
        appearance.appearanceFlags |=
            ((1 << AppearanceFlagQuadruped) | (1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceWhiteCat:
        appearance.headModel = SpriteCatHead3;
        appearance.chestModel = SpriteCatBody3;
        appearance.feetModel = SpriteCatFoot3;
        appearance.handsModel = SpriteCatHand3;
        appearance.tailModel = SpriteCatTail3;
        appearance.headScale = 0.9f;
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 9.0f, -1.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, -4.0f);
        appearance.handsPosition = plasma::Vector<3, float>(3.0f, 4.0f, -8.5f);
        appearance.feetPosition = plasma::Vector<3, float>(3.0f, -4.1f, -8.5f);
        appearance.feetScale = 1.0f;
        appearance.handsScale = 1.0f;
        appearance.tailPosition = plasma::Vector<3, float>(0.0f, -8.0f, -1.0f);
        appearance.tailScale = 1.1f;
        appearance.tailRotation = -30.0f;
        appearance.appearanceFlags |=
            ((1 << AppearanceFlagQuadruped) | (1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    case RacePig:
        appearance.headModel = SpritePigHead;
        appearance.chestModel = SpritePigBody;
        appearance.feetModel = SpritePigFoot;
        appearance.handsModel = SpritePigFoot;
        appearance.size = plasma::Vector<3, float>(0.8f, 0.8f, 0.8f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 9.0f, 1.9f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 1.0f);
        appearance.handsPosition = plasma::Vector<3, float>(3.0f, 4.0f, -4.2f);
        appearance.feetPosition = plasma::Vector<3, float>(3.0f, -4.0f, -4.2f);
        appearance.chestScale = 1.2f;
        appearance.headScale = 0.85f;
        appearance.feetScale = 1.0f;
        appearance.handsScale = 1.0f;
        appearance.tailPosition = plasma::Vector<3, float>(0.0f, -8.0f, -1.0f);
        appearance.tailScale = 1.0f;
        appearance.tailRotation = -30.0f;
        appearance.appearanceFlags |=
            ((1 << AppearanceFlagQuadruped) | (1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceSheep:
        appearance.headModel = SpriteSheepHead;
        appearance.chestModel = SpriteSheepBody;
        appearance.feetModel = SpriteSheepFoot;
        appearance.handsModel = SpriteSheepFoot;
        appearance.size = plasma::Vector<3, float>(1.2f, 1.2f, 1.2f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 4.0f, 1.9f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 1.0f);
        appearance.handsPosition = plasma::Vector<3, float>(3.0f, 4.0f, -4.2f);
        appearance.feetPosition = plasma::Vector<3, float>(3.0f, -4.0f, -4.2f);
        appearance.chestScale = 1.2f;
        appearance.headScale = 0.85f;
        appearance.feetScale = 1.0f;
        appearance.handsScale = 1.0f;
        appearance.tailPosition = plasma::Vector<3, float>(0.0f, -8.0f, -1.0f);
        appearance.tailScale = 1.0f;
        appearance.tailRotation = -30.0f;
        appearance.appearanceFlags |=
            ((1 << AppearanceFlagQuadruped) | (1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceDragon:
        appearance.headModel = SpriteDragonHead;
        appearance.chestModel = SpriteDragonBody;
        appearance.feetModel = SpriteDragonFoot;
        appearance.handsModel = SpriteDragonFoot;
        appearance.tailModel = SpriteDragonTail;
        appearance.headScale = 0.51f;
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 12.5f, 0.5f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, -2.0f);
        appearance.handsPosition = plasma::Vector<3, float>(3.0f, 4.0f, 1.0f);
        appearance.feetPosition = plasma::Vector<3, float>(3.0f, -4.0f, 1.0f);
        appearance.feetScale = 1.0f;
        appearance.handsScale = 1.5f;
        appearance.tailPosition = plasma::Vector<3, float>(0.0f, -16.0f, 4.0f);
        appearance.appearanceFlags |= ((1 << AppearanceFlagQuadruped) | (1 << AppearanceFlagFlying) |
                                       (1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceHornet:
        appearance.headModel = SpriteHornetHead;
        appearance.chestModel = SpriteHornetBody;
        appearance.feetModel = SpriteHornetFoot;
        appearance.wingsModel = SpriteHornetHand;
        appearance.feetPosition = plasma::Vector<3, float>(3.0f, -1.0f, -7.0f);
        appearance.chestRotation = -45.0f;
        appearance.feetRotation = 30.0f;
        appearance.wingsRotation = 45.0f;
        appearance.appearanceFlags |= (1 << AppearanceFlagFlying);
        appearance.wingsPosition = plasma::Vector<3, float>(1.0f, -6.0f, 1.0f);
        appearance.appearanceFlags |= ((1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceInsectGuard:
        appearance.headModel = SpriteInsectGuardHead;
        appearance.chestModel = SpriteInsectGuardBody;
        appearance.feetModel = SpriteInsectGuardFoot;
        appearance.handsModel = SpriteInsectGuardHand;
        appearance.wingsModel = SpriteInsectGuardWing;
        appearance.feetPosition = plasma::Vector<3, float>(3.0f, -1.0f, -7.0f);
        appearance.chestRotation = -45.0f;
        appearance.feetRotation = 30.0f;
        appearance.wingsRotation = 45.0f;
        appearance.appearanceFlags |= (1 << AppearanceFlagFlying);
        appearance.wingsPosition = plasma::Vector<3, float>(1.0f, -6.0f, 1.0f);
        appearance.appearanceFlags |= ((1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceFly:
        appearance.headModel = SpriteFlyHead;
        appearance.chestModel = SpriteFlyBody;
        appearance.feetModel = SpriteFlyFoot;
        appearance.wingsModel = SpriteFlyHand;
        appearance.feetPosition = plasma::Vector<3, float>(3.0f, -1.0f, -7.0f);
        appearance.chestRotation = -45.0f;
        appearance.feetRotation = 30.0f;
        appearance.wingsRotation = 45.0f;
        appearance.appearanceFlags |= (1 << AppearanceFlagFlying);
        appearance.wingsPosition = plasma::Vector<3, float>(1.0f, -6.0f, 1.0f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 3.0f, 4.0f);
        appearance.appearanceFlags |= ((1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceBumblebee:
        appearance.headModel = SpriteBumblebee;
        appearance.feetModel = SpriteBumblebeeFoot;
        appearance.wingsModel = SpriteBumblebeeHand;
        appearance.appearanceFlags |= (1 << AppearanceFlagFlying);
        appearance.feetPosition = plasma::Vector<3, float>(3.0f, 4.0f, -8.0f);
        appearance.chestRotation = -45.0f;
        appearance.wingsRotation = 45.0f;
        appearance.headScale = 1.2f;
        appearance.wingScale = 0.6f;
        appearance.feetScale = 1.0f;
        appearance.size = plasma::Vector<3, float>(1.0f, 1.0f, 1.875f);
        appearance.wingsPosition = plasma::Vector<3, float>(6.5f, 3.0f, -1.0f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 3.0f, 0.0f);
        appearance.appearanceFlags |= ((1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceMidge:
        appearance.headModel = SpriteMidgeHead;
        appearance.chestModel = SpriteMidgeBody;
        appearance.feetModel = SpriteMidgeFoot;
        appearance.wingsModel = SpriteMidgeHand;
        appearance.feetPosition = plasma::Vector<3, float>(3.0f, -1.0f, -5.0f);
        appearance.chestRotation = -45.0f;
        appearance.feetRotation = 30.0f;
        appearance.wingsRotation = 45.0f;
        appearance.appearanceFlags |= (1 << AppearanceFlagFlying);
        appearance.wingsPosition = plasma::Vector<3, float>(1.0f, -5.0f, -1.0f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 3.0f, 4.0f);
        appearance.appearanceFlags |= ((1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceMosquito:
        appearance.headModel = SpriteMosquitoHead;
        appearance.chestModel = SpriteMosquitoBody;
        appearance.feetModel = SpriteMosquitoFoot;
        appearance.wingsModel = SpriteMosquitoHand;
        appearance.feetPosition = plasma::Vector<3, float>(3.0f, -1.0f, -7.0f);
        appearance.chestRotation = -45.0f;
        appearance.feetRotation = 30.0f;
        appearance.wingsRotation = 45.0f;
        appearance.appearanceFlags |= (1 << AppearanceFlagFlying);
        appearance.wingsPosition = plasma::Vector<3, float>(1.0f, -6.0f, 1.0f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 3.0f, 4.0f);
        appearance.appearanceFlags |= ((1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceCrow:
        appearance.headModel = SpriteCrowHead;
        appearance.chestModel = SpriteCrowBody;
        appearance.feetModel = SpriteCrowFoot;
        appearance.wingsModel = SpriteCrowHand;
        appearance.feetPosition = plasma::Vector<3, float>(3.0f, -4.0f, -10.0f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 3.0f, 6.0f);
        appearance.chestRotation = -45.0f;
        appearance.wingsRotation = 45.0f;
        appearance.appearanceFlags |= (1 << AppearanceFlagFlying);
        appearance.wingsPosition = plasma::Vector<3, float>(3.0f, -5.0f, 0.0f);
        appearance.appearanceFlags |= ((1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceChicken:
        appearance.headModel = SpriteChickenHead;
        appearance.chestModel = SpriteChickenBody;
        appearance.feetModel = SpriteChickenFoot;
        appearance.wingsModel = SpriteChickenHand;
        appearance.feetPosition = plasma::Vector<3, float>(3.0f, 0.0f, -10.0f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 3.0f, 2.0f);
        appearance.chestScale = 1.3f;
        appearance.chestRotation = 0.0f;
        appearance.wingsRotation = 45.0f;
        appearance.appearanceFlags |= (1 << AppearanceFlagFlying);
        appearance.wingsPosition = plasma::Vector<3, float>(3.0f, -2.0f, -5.0f);
        appearance.size = plasma::Vector<3, float>(0.8f, 0.8f, 1.65f);
        appearance.appearanceFlags |= ((1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceSeagull:
        appearance.headModel = SpriteSeagullHead;
        appearance.chestModel = SpriteSeagullBody;
        appearance.feetModel = SpriteSeagullFoot;
        appearance.wingsModel = SpriteSeagullHand;
        appearance.feetPosition = plasma::Vector<3, float>(3.0f, -4.0f, -10.0f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 5.0f, 8.0f);
        appearance.chestRotation = -45.0f;
        appearance.wingsRotation = 45.0f;
        appearance.appearanceFlags |= (1 << AppearanceFlagFlying);
        appearance.wingsPosition = plasma::Vector<3, float>(3.0f, -5.0f, 0.0f);
        appearance.appearanceFlags |= ((1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceParrot:
        if (rand() % 2) {
            appearance.headModel = SpriteParrotHead;
            appearance.chestModel = SpriteParrotBody;
            appearance.feetModel = SpriteParrotFoot;
            appearance.wingsModel = SpriteParrotHand;
        } else {
            appearance.headModel = SpriteParrotBlueHead;
            appearance.chestModel = SpriteParrotBlueBody;
            appearance.feetModel = SpriteParrotBlueFoot;
            appearance.wingsModel = SpriteParrotBlueHand;
        }
        appearance.feetPosition = plasma::Vector<3, float>(3.0f, -4.0f, -10.0f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 3.0f, 6.0f);
        appearance.chestRotation = -45.0f;
        appearance.wingsRotation = 45.0f;
        appearance.appearanceFlags |= (1 << AppearanceFlagFlying);
        appearance.wingsPosition = plasma::Vector<3, float>(3.0f, -5.0f, 0.0f);
        appearance.appearanceFlags |= ((1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceBat:
        appearance.headModel = SpriteBatHead;
        appearance.chestModel = SpriteBatBody;
        appearance.feetModel = SpriteBatFoot;
        appearance.wingsModel = SpriteBatHand;
        appearance.feetPosition = plasma::Vector<3, float>(3.0f, -4.0f, -10.0f);
        appearance.chestRotation = -45.0f;
        appearance.wingsRotation = 45.0f;
        appearance.appearanceFlags |= (1 << AppearanceFlagFlying);
        appearance.wingsPosition = plasma::Vector<3, float>(3.0f, -5.0f, 0.0f);
        appearance.appearanceFlags |= ((1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    case RacePlainRunner:
        appearance.chestModel = SpritePlainrunnerBody;
        appearance.feetModel = SpritePlainrunnerFoot;
        appearance.handsModel = SpriteNone;
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 3.0f, 6.0f);
        appearance.feetPosition = plasma::Vector<3, float>(4.0f, 0.0f, -1.5f);
        appearance.size = plasma::Vector<3, float>(0.8f, 0.8f, 1.8f) * 1.5f;
        appearance.feetScale = 1.5f;
        appearance.appearanceFlags |= ((1 << AppearanceFlagHandAttacks) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceLeafRunner:
        appearance.chestModel = SpriteLeafrunnerBody;
        appearance.feetModel = SpriteLeafrunnerFoot;
        appearance.handsModel = SpriteNone;
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 3.0f, 6.0f);
        appearance.feetPosition = plasma::Vector<3, float>(4.0f, 0.0f, -1.5f);
        appearance.size = plasma::Vector<3, float>(0.8f, 0.8f, 1.8f) * 1.5f;
        appearance.feetScale = 1.5f;
        appearance.appearanceFlags |= ((1 << AppearanceFlagHandAttacks) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceSnowRunner:
        appearance.chestModel = SpriteSnowrunnerBody;
        appearance.feetModel = SpriteSnowrunnerFoot;
        appearance.handsModel = SpriteNone;
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 3.0f, 6.0f);
        appearance.feetPosition = plasma::Vector<3, float>(4.0f, 0.0f, -1.5f);
        appearance.size = plasma::Vector<3, float>(0.8f, 0.8f, 1.8f) * 1.5f;
        appearance.feetScale = 1.5f;
        appearance.appearanceFlags |= ((1 << AppearanceFlagHandAttacks) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceDesertRunner:
        appearance.chestModel = SpriteDesertrunnerBody;
        appearance.feetModel = SpriteDesertrunnerFoot;
        appearance.handsModel = SpriteNone;
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 3.0f, 6.0f);
        appearance.feetPosition = plasma::Vector<3, float>(4.0f, 0.0f, -1.5f);
        appearance.size = plasma::Vector<3, float>(0.8f, 0.8f, 1.8f) * 1.5f;
        appearance.feetScale = 1.5f;
        appearance.appearanceFlags |= ((1 << AppearanceFlagHandAttacks) | (1 << AppearanceFlagUnarmored));
        break;
    case RacePeacock:
        appearance.headModel = SpritePeacockHead;
        appearance.chestModel = SpritePeacockBody;
        appearance.feetModel = SpritePeacockFoot;
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 4.0f);
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 4.0f, 8.0f);
        appearance.feetPosition = plasma::Vector<3, float>(4.0f, 0.0f, -6.0f);
        appearance.size = plasma::Vector<3, float>(1.2f, 1.2f, 3.75f);
        appearance.chestScale = 0.7f;
        appearance.headScale = 0.5f;
        appearance.feetScale = 1.5f;
        appearance.appearanceFlags |= ((1 << AppearanceFlagHandAttacks) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceBush:
        appearance.size = plasma::Vector<3, float>(2.0f, 2.0f, 2.0f);
        appearance.chestModel = SpriteBush;
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
        break;
    case RaceSnowBush:
        appearance.size = plasma::Vector<3, float>(2.0f, 2.0f, 2.0f);
        appearance.chestModel = SpriteSnowBush;
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
        break;
    case RaceCobwebScrub:
        appearance.size = plasma::Vector<3, float>(0.5f, 0.5f, 1.7f);
        appearance.chestModel = SpriteCobwebscrub;
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
        appearance.chestScale = 1.5f;
        break;
    case RaceSnowBerryBush:
        appearance.size = plasma::Vector<3, float>(2.0f, 2.0f, 2.0f);
        appearance.chestModel = SpriteBerryBush;
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
        break;
    case RaceCottonPlant:
        appearance.size = plasma::Vector<3, float>(0.5f, 0.5f, 1.5f);
        appearance.chestModel = SpriteCottonPlant;
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
        break;
    case RaceEgg:
        appearance.size = plasma::Vector<3, float>(0.8f, 0.8f, 1.2f);
        appearance.chestModel = SpriteEgg1;
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, -1.5f);
        appearance.appearanceFlags |= (1 << AppearanceFlagUnarmored);
        break;
    case RaceBomb:
        appearance.size = plasma::Vector<3, float>(0.8f, 0.8f, 0.8f);
        appearance.chestModel = SpriteBomb1;
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 1.0f);
        appearance.chestScale = 0.8f;
        break;
    case RaceScrub:
        appearance.size = plasma::Vector<3, float>(2.0f, 2.0f, 2.0f);
        appearance.chestModel = rand() % 2 ? SpriteScrub : SpriteScrubGreen;
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 2.0f);
        break;
    case RaceFireScrub:
        appearance.size = plasma::Vector<3, float>(2.0f, 2.0f, 2.0f);
        appearance.chestModel = SpriteFireScrub;
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 2.0f);
        break;
    case RaceGinseng:
        appearance.size = plasma::Vector<3, float>(1.0f, 1.0f, 1.0f);
        appearance.chestModel = SpriteGinseng;
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
        break;
    case RaceCactus:
        appearance.size = plasma::Vector<3, float>(1.5f, 1.5f, 4.0f);
        appearance.chestModel = SpriteCactus1;
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
        break;
    case RaceVase:
        appearance.size = plasma::Vector<3, float>(1.5f, 1.5f, 1.25f);
        appearance.chestModel = rand() % VaseModelVariants + SpriteVase;
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
        break;
    case RaceFirTree:
        appearance.chestModel = SpriteFirTree;
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, -4.0f);
        break;
    case RaceThornTree:
        appearance.chestModel = SpriteThornTree;
        appearance.size = plasma::Vector<3, float>(3.0f, 3.0f, 12.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, -4.0f);
        break;
    case RaceGoldDeposit:
        appearance.chestModel = SpriteGoldDeposit;
        appearance.size = plasma::Vector<3, float>(2.0f, 2.0f, 2.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, -4.0f);
        break;
    case RaceIronDeposit:
        appearance.chestModel = SpriteIronDeposit;
        appearance.size = plasma::Vector<3, float>(2.0f, 2.0f, 2.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, -4.0f);
        break;
    case RaceSilverDeposit:
        appearance.chestModel = SpriteSilverDeposit;
        appearance.size = plasma::Vector<3, float>(2.0f, 2.0f, 2.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, -4.0f);
        break;
    case RaceSandstoneDeposit:
        appearance.chestModel = SpriteSandstoneDeposit;
        appearance.size = plasma::Vector<3, float>(2.0f, 2.0f, 2.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, -4.0f);
        break;
    case RaceEmeraldDeposit:
        appearance.chestModel = SpriteEmeraldDeposit;
        appearance.size = plasma::Vector<3, float>(2.0f, 2.0f, 2.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, -4.0f);
        break;
    case RaceSapphireDeposit:
        appearance.chestModel = SpriteSapphireDeposit;
        appearance.size = plasma::Vector<3, float>(2.0f, 2.0f, 2.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, -4.0f);
        break;
    case RaceRubyDeposit:
        appearance.chestModel = SpriteRubyDeposit;
        appearance.size = plasma::Vector<3, float>(2.0f, 2.0f, 2.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, -4.0f);
        break;
    case RaceDiamondDeposit:
        appearance.chestModel = SpriteDiamondDeposit;
        appearance.size = plasma::Vector<3, float>(2.0f, 2.0f, 2.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, -4.0f);
        break;
    case RaceIceCrystalDeposit:
        appearance.chestModel = SpriteIceCrystalDeposit;
        appearance.size = plasma::Vector<3, float>(2.0f, 2.0f, 2.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, -4.0f);
        break;
    case RaceScarecrow:
        appearance.chestModel = SpriteScarecrow;
        appearance.size = plasma::Vector<3, float>(0.8f, 0.8f, 4.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
        appearance.chestScale = 2.0f;
        appearance.appearanceFlags |= (1 << AppearanceFlagUnarmored);
        break;
    case RaceAim:
        appearance.chestModel = SpriteAim;
        appearance.size = plasma::Vector<3, float>(1.5f, 1.5f, 2.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
        appearance.appearanceFlags |= (1 << AppearanceFlagUnarmored);
        break;
    case RaceDummy:
        appearance.chestModel = SpriteDummy;
        appearance.size = plasma::Vector<3, float>(1.5f, 1.5f, 2.5f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
        appearance.appearanceFlags |= (1 << AppearanceFlagUnarmored);
        break;
    case RaceMammoth: {
        float width = rand() * 3.0f / float(RAND_MAX) + 7.0f;
        appearance.size = plasma::Vector<3, float>(width, width, width * 1.12f);
    }
        appearance.headModel = SpriteMammothHead;
        appearance.chestModel = SpriteMammothBody;
        appearance.feetModel = SpriteMammothFoot;
        appearance.handsModel = SpriteMammothFoot;
        appearance.chestScale = 0.7f;
        appearance.headScale = 0.4f;
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 8.0f, 2.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 1.0f);
        appearance.tailPosition = plasma::Vector<3, float>(0.0f, -4.0f, -3.0f);
        appearance.feetScale = 0.5f;
        appearance.handsScale = 0.5f;
        appearance.handsPosition = plasma::Vector<3, float>(3.0f, 3.0f, -4.1f);
        appearance.feetPosition = plasma::Vector<3, float>(2.9f, -3.0f, -4.1f);
        appearance.appearanceFlags |=
            ((1 << AppearanceFlagQuadruped) | (1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    case RaceSaurian:
        appearance.size = plasma::Vector<3, float>(9.0f, 9.0f, 10.08f);
        appearance.headModel = rand() % MonsterHeadReptileLizardVariantCount + SpriteMonsterHeadReptileLizard;
        appearance.chestModel = rand() % MonsterBodyReptileShellVariantCount + SpriteMonsterBodyReptileShell;
        appearance.handsModel = rand() % MonsterFootReptileNormalVariantCount + SpriteMonsterFootReptileNormal;
        appearance.feetModel = appearance.handsModel;
        appearance.tailModel = rand() % MonsterTailReptileNormalVariantCount + SpriteMonsterTailReptileNormal;
        appearance.tailScale = 0.7f;
        appearance.chestScale = 0.7f;
        appearance.headScale = rand() * 0.1f / float(RAND_MAX) + 0.4f;
        appearance.headPosition = plasma::Vector<3, float>(0.0f, 8.0f, rand() * 0.5f / float(RAND_MAX) - 1.0f);
        appearance.chestPosition = plasma::Vector<3, float>(0.0f, 0.0f, 1.0f);
        appearance.tailPosition = plasma::Vector<3, float>(0.0f, -4.0f, -3.0f);
        appearance.feetScale = 0.5f;
        appearance.handsScale = 0.5f;
        appearance.handsPosition = plasma::Vector<3, float>(rand() / float(RAND_MAX) + 3.0f, 3.0f, -4.1f);
        appearance.feetPosition = plasma::Vector<3, float>(rand() / float(RAND_MAX) + 3.0f, -3.0f, -4.1f);
        appearance.appearanceFlags |=
            ((1 << AppearanceFlagQuadruped) | (1 << AppearanceFlagBiteAttack) | (1 << AppearanceFlagUnarmored));
        break;
    default:
        break;
    }
}

Creature::Buff *Creature::findBuff(int type)
{
    for (std::list<Buff>::iterator it = m_buffs.begin(); it != m_buffs.end(); ++it) {
        if (it->type == type)
            return &*it;
    }
    return 0;
}

void raceGroupSizeRange(int race, int &minimum, int &maximum)
{
    minimum = 1;
    maximum = 1;
    switch (race) {
    case RaceAlpaca:
    case RaceBunny:
    case RacePorcupine:
    case RaceInsectGuard:
    case RaceChicken:
    case RaceFly:
    case RaceOnionling:
    case RaceDesertOnionling:
    case RaceDevourer:
    case RaceHorse:
    case RaceCamel:
    case RaceCow:
    case RaceBarkBeetle:
    case RaceFireBeetle:
    case RaceSnoutBeetle:
    case RaceLemonBeetle:
    case RacePiranha:
        minimum = 1;
        maximum = LargeSpawnGroupMaximum;
        break;
    case RaceSkullBull:
    case RaceWolf:
    case RaceSandHorror:
    case RaceMonkey:
    case RaceCrow:
    case RacePlainRunner:
    case RacePlantCreature:
    case RaceRadishCreature:
    case RaceMole:
    case RaceBiter:
        minimum = 1;
        maximum = SmallSpawnGroupMaximum;
        break;
    }
}

// Authored spawn-level bands by race; these endpoints are independent of the smooth level/stat
// curves.
void raceLevelRange(int race, int &minimum, int &maximum)
{
    minimum = 1;
    maximum = 1;
    switch (race) {
    case RaceCollie:
    case RaceShepherdDog:
    case RaceTurtle:
    case RaceTerrier:
    case RaceCat:
    case RaceBrownCat:
    case RaceWhiteCat:
    case RaceFly:
    case RacePlainRunner:
    case RacePeacock:
    case RacePlantCreature:
    case RaceRadishCreature:
        minimum = 3;
        maximum = 6;
        break;
    case RaceHorse:
    case RaceCow:
        minimum = 6;
        maximum = 9;
        break;
    case RaceGreenSlime:
    case RacePinkSlime:
    case RaceYellowSlime:
    case RaceBlueSlime:
    case RaceHornet:
    case RaceBat:
    case RaceBiter:
    case RaceLemonBeetle:
    case RaceCrab:
    case RaceBumblebee:
        minimum = 9;
        maximum = 14;
        break;
    case RaceWolf:
    case RaceMidge:
    case RaceSquirrel:
    case RaceBlowfish:
        minimum = 14;
        maximum = 21;
        break;
    case RacePorcupine:
    case RaceInsectGuard:
    case RaceLeafRunner:
    case RaceSnowRunner:
    case RaceDesertRunner:
    case RaceDesertOnionling:
    case RaceDevourer:
    case RaceRaccoon:
    case RaceCamel:
    case RaceLanternFish:
    case RaceMawFish:
        minimum = 21;
        maximum = 31;
        break;
    case RaceSkullBull:
    case RaceOgre:
    case RaceRockling:
    case RaceMonkey:
    case RaceParrot:
    case RaceCrocodile:
    case RaceSpectrino:
    case RaceSpitter:
    case RaceKoala:
    case RaceBarkBeetle:
    case RaceSnoutBeetle:
    case RaceShark:
        minimum = 31;
        maximum = 47;
        break;
    case RaceSkeleton:
    case RacePolarGnoll:
    case RaceFireBeetle:
    case RaceSnowGolem:
    case RaceYeti:
    case RacePiranha:
        minimum = 47;
        maximum = 81;
        break;
    case RaceDjinn:
    case RaceNomadMale:
    case RaceNomadFemale:
    case RaceWerewolf:
        minimum = 81;
        maximum = 180;
        break;
    case RaceMosquito:
    case RaceMinotaur:
    case RaceImp:
    case RaceVampire:
    case RaceHellDemon:
    case RaceEmberGolem:
        minimum = 180;
        maximum = 19981;
        break;
    default:
        minimum = 2;
        maximum = 4;
        break;
    }
}

bool Creature::hasRandomChance(float chance)
{
    if (m_entityData.equipment.weaponRight.category == ItemCategoryWeapon) {
        switch (m_entityData.equipment.weaponRight.id) {
        case WeaponBoomerang:
        case WeaponStaff:
            chance *= 0.3f;
            break;
        case WeaponSword:
        case WeaponAxe:
        case WeaponMace:
        case WeaponBracelet:
            chance *= 0.5f;
            break;
        }
    }
    return chance > rand() / float(RAND_MAX);
}

bool Creature::hasTwoHandedWeapon()
{
    return m_entityData.equipment.weaponRight.isTwoHanded();
}

int Creature::holdTime(int ability)
{
    if (ability < 0)
        ability = m_entityData.currentAbility;
    switch (ability) {
    case AbilityNone:
    case AbilityRetreat:
    case AbilityShuriken:
        return NoneGroupHoldMs;
    case AbilityTwoHandedSweep:
    case AbilityTwoHandedThrust:
        return (int)(300.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilitySwirlStrike:
    case AbilityCrushStrike:
    case AbilityGuardianCrushStrike:
    case AbilityBlockStrike:
        return (int)(100.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityTwoHandedSpin:
    case AbilityTwoHandedSlam:
        return (int)(200.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityHeadbutt:
    case AbilityHornCharge:
    case Ability93:
        return (int)(800.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityBlockJab:
        return BlockJabHoldMs;
    case Ability3:
    case Ability4:
    case AbilityPerforate:
    case Ability62:
        return (int)(300.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityLeftStab:
    case AbilityRightSweep:
    case AbilityLeftDaggerPierce:
        return (int)(200.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityOverheadChop:
        return (int)(400.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilitySmash:
        return SmashHoldMs;
    case AbilityIntercept:
        return 0;
    case AbilityShellBlock:
    case AbilityShellSpin:
        return ShellBlockGroupHoldMs;
    case AbilityTwoHandedSwing:
        return (int)(100.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityShoot:
    case AbilityThrowBoomerang:
    case AbilityFireSwirl:
    case AbilityFireVortex:
    case AbilityWaterSwirl:
    case AbilityWaterVortex:
    case AbilityHealingStream:
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
    case AbilityFireBeam:
    case AbilityFireRay:
        return (int)(100.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityChargedShotRelease:
        return (int)(10.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    default:
        return (int)(500.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    }
}

// Skill cooldowns are never held in this build.
bool Creature::holdsCooldown(int)
{
    return false;
}

bool Creature::rollCritical()
{
    float chance = criticalChance();
    return chance > rand() / float(RAND_MAX);
}

bool Creature::isAttackingFromRange()
{
    if (isCaster())
        return true;
    if (m_entityData.race == RaceSnoutBeetle)
        return true;
    if (m_entityData.equipment.weaponRight.category == ItemCategoryWeapon &&
        (m_entityData.equipment.weaponRight.id == WeaponBow ||
         m_entityData.equipment.weaponRight.id == WeaponCrossbow ||
         m_entityData.equipment.weaponRight.id == WeaponBoomerang ||
         m_entityData.equipment.weaponRight.id == WeaponStaff || m_entityData.equipment.weaponRight.id == WeaponWand))
        return true;
    return false;
}

bool Creature::isBlocking()
{
    if (m_entityData.actionLockTime > 0)
        return false;
    if (!(m_entityData.currentAbility == AbilityShellBlock || m_entityData.currentAbility == AbilityShellSpin ||
          m_entityData.currentAbility == AbilityBlock || m_entityData.currentAbility == AbilityParry ||
          ((m_entityData.currentAbility == AbilityCrush || m_entityData.currentAbility == AbilitySwirl) &&
           m_entityData.classType == ClassWarrior && m_entityData.specialization == SpecializationGuardian)))
        return false;
    return true;
}

// The bosses' races.
bool Creature::isBoss()
{
    int race = m_entityData.race;
    return race == RaceTroll || race == RaceDarkTroll || race == RaceYeti || race == RaceMammoth ||
           race == RaceCyclops || race == RaceRuneGiant || race == RaceSeaCrab || race == RaceLich ||
           race == RaceDragon || race == RaceSaurian;
}

bool Creature::isCaster()
{
    if (m_entityData.classType == ClassMage || m_entityData.race == RaceLich || m_entityData.race == RaceSpitter)
        return true;
    if (m_entityData.equipment.weaponRight.category == ItemCategoryWeapon &&
        (m_entityData.equipment.weaponRight.id == WeaponStaff || m_entityData.equipment.weaponRight.id == WeaponWand ||
         m_entityData.equipment.weaponRight.id == WeaponBracelet))
        return true;
    return false;
}

// Gliding: the glider is out and the creature is falling, in the air, and neither stunned nor
// rolling.
bool Creature::isGliding()
{
    return m_entityData.binaryToggles.gliding && m_entityData.velocity[2] < 0.0f &&
           !(m_entityData.flags & (1 << CreatureFlagOnGround)) && m_entityData.stunTime <= 0 &&
           m_entityData.rollTime <= 0;
}

// Animals and other harmless races; creatures with AppearanceFlagTinted, AppearanceFlagSummoned or
// AppearanceFlagMissionLevel never are.
bool Creature::isPeaceful()
{
    if (!(m_entityData.appearance.appearanceFlags & (1 << AppearanceFlagTinted)) &&
        !(m_entityData.appearance.appearanceFlags & (1 << AppearanceFlagSummoned)) &&
        !(m_entityData.appearance.appearanceFlags & (1 << AppearanceFlagMissionLevel))) {
        int race = m_entityData.race;
        if (race == RaceOwl || race == RaceDuckbill || race == RaceSheep || race == RaceTurtle || race == RaceCrow ||
            race == RaceHornet || race == RaceMole || race == RaceFrog || race == RacePeacock || race == RacePenguin ||
            race == RaceCrab || race == RaceSeaCrab || race == RaceBunny || race == RaceParrot || race == RaceSeagull ||
            race == RaceSeahorse || race == RaceSapphireFish || race == RaceLemonFish || race == RaceAlpaca ||
            race == RaceBrownAlpaca || race == RaceHorse || race == RaceChicken || race == RaceCat ||
            race == RaceBrownCat || race == RaceWhiteCat || race == RaceCollie || race == RaceTerrier ||
            race == RaceScottishTerrier || race == RacePig || race == RaceCow || race == RaceShepherdDog ||
            race == RaceKoala)
            return true;
    }
    return false;
}

bool isPet(int race)
{
    switch (race) {
    case RaceCollie:
    case RaceShepherdDog:
    case RaceAlpaca:
    case RaceBrownAlpaca:
    case RaceTurtle:
    case RaceTerrier:
    case RaceScottishTerrier:
    case RaceWolf:
    case RacePanther:
    case RaceCat:
    case RaceBrownCat:
    case RaceWhiteCat:
    case RacePig:
    case RaceSheep:
    case RaceBunny:
    case RacePorcupine:
    case RaceGreenSlime:
    case RacePinkSlime:
    case RaceYellowSlime:
    case RaceBlueSlime:
    case RacePlainRunner:
    case RaceLeafRunner:
    case RaceSnowRunner:
    case RaceDesertRunner:
    case RacePeacock:
    case RaceDuckbill:
    case RaceCrocodile:
    case RaceHorse:
    case RaceCamel:
    case RaceCow:
    case RaceBarkBeetle:
    case RaceFireBeetle:
    case RaceSnoutBeetle:
    case RaceLemonBeetle:
    case RaceBumblebee:
        return true;
    }
    return false;
}

float Creature::knockback(int ability)
{
    if (m_entityData.hostilityType == HostilityPet || m_entityData.hostilityType == HostilityFriendlyNpc)
        return 0.0f;
    switch (ability) {
    case AbilityRightStab:
    case AbilityLeftStab:
    case AbilityLeftDaggerPierce:
    case AbilityRightDaggerPierce:
        return 3.0f / tempo();
    case AbilityLeftSweep:
    case AbilityRightSweep:
    case AbilityOverheadChop:
        return 6.0f / tempo();
    case AbilitySwirlStrike:
    case AbilityExplosion:
        return 10.0f;
    case AbilitySmash:
        return 5.0f;
    case Ability3:
    case Ability4:
    case Ability62:
        return 3.0f / tempo();
    case AbilityRightSlash:
    case AbilityLeftSlash:
    case AbilityShieldAttack:
        return 6.0f / tempo();
    case AbilityAmbush:
    case AbilityKick:
        return 6.0f;
    case AbilityRangerKick:
        return abilitySkillBonus(AbilityRangerKick, -1) * 30.0f + 5.0f;
    case AbilityFireExplosion:
        return abilitySkillBonus(AbilityRangerKick, -1) * 30.0f + 5.0f;
    case AbilityPerforate:
        return 4.0f;
    case AbilityCrushStrike:
    case AbilityGuardianCrushStrike:
        return 6.0f / tempo();
    case AbilityTwoHandedSpin:
    case AbilityTwoHandedSlam:
        return 10.0f / tempo();
    default:
        return 2.0f / tempo();
    case AbilityFireSwirl:
    case AbilityFireVortex:
    case AbilityWaterSwirl:
    case AbilityWaterVortex:
        return 0.0f;
    }
}

bool Creature::knowsFormula(const Item &item)
{
    if (!m_playerData)
        return false;
    for (std::list<Item>::iterator it = m_playerData->knownFormulas.begin(); it != m_playerData->knownFormulas.end();
         ++it) {
        if (item == *it)
            return true;
    }
    return false;
}

float Creature::manaCost(int ability, int points)
{
    switch (ability) {
    case Ability3:
    case Ability4:
        return 0.1f;
    case AbilityFireVortex:
    case AbilityWaterVortex:
    case AbilityFireball:
    case AbilityWaterSplash:
    case AbilityWaterSalvo:
    case AbilityFireSalvo:
    case AbilityFireRay:
        if (!findBuff(BuffInstantFire))
            return 0.3f;
        break;
    case AbilityPerforate:
    case AbilityFireSwirl:
    case AbilityWaterSwirl:
        return 0.0f;
    case AbilityHealingStream:
        return (1.0f - abilitySkillBonus(AbilityHealingStream, points) * 0.75f) * 0.125f;
    }
    return 0.0f;
}

long long Creature::maxAggroTarget()
{
    float maximum = 0.0f;
    long long target = 0;
    for (std::map<long long, float>::iterator it = m_aggro.begin(); it != m_aggro.end(); ++it) {
        if (it->first != 0 && it->second >= maximum) {
            maximum = it->second;
            target = it->first;
        }
    }
    return target;
}

int Creature::maxCombo()
{
    if (m_entityData.equipment.weaponRight.category == ItemCategoryWeapon) {
        switch (m_entityData.equipment.weaponRight.id) {
        case WeaponWand:
            return (int)(m_entityData.multipliers.attackSpeed * 20.0f);
        case WeaponBracelet:
            return (int)(m_entityData.multipliers.attackSpeed * 20.0f);
        case WeaponGreatsword:
            return (int)(m_entityData.multipliers.attackSpeed * 20.0f);
        case WeaponGreataxe:
            return (int)(m_entityData.multipliers.attackSpeed * 20.0f);
        case WeaponGreatmace:
            return (int)(m_entityData.multipliers.attackSpeed * 20.0f);
        case WeaponBoomerang:
            return (int)(m_entityData.multipliers.attackSpeed * 80.0f);
        case WeaponBow:
            return (int)(m_entityData.multipliers.attackSpeed * 60.0f);
        }
    } else if (m_entityData.appearance.appearanceFlags & (1 << AppearanceFlagBiteAttack)) {
        return (int)(m_entityData.multipliers.attackSpeed * 20.0f);
    }
    return (int)(m_entityData.multipliers.attackSpeed * 50.0f);
}

int Creature::maxExperience()
{
    return (1.0f - 1.0f / ((m_entityData.level - 1.0f) * 0.05f + 1.0f)) * 1000.0f + 50.0f;
}

float Creature::maxHP()
{
    float hp = itemPowerScale((float)m_entityData.level, m_entityData.rarity) * m_entityData.multipliers.hp;
    if (m_entityData.hostilityType == HostilityPlayer)
        hp = itemPowerScale((float)m_entityData.level, ItemRarityYellow) * m_entityData.multipliers.hp;
    switch (m_entityData.classType) {
    case ClassWarrior:
        hp *= 1.3f;
        break;
    case ClassRogue:
        hp *= 1.2f;
        break;
    case ClassRanger:
        hp *= 1.1f;
        break;
    case ClassMage:
        hp *= 1.0f;
        break;
    }
    if (m_entityData.classType == ClassWarrior && m_entityData.specialization == SpecializationGuardian)
        hp *= 1.25f;
    Equipment &equipment = m_entityData.equipment;
    if (equipment.weaponLeft.category == ItemCategoryWeapon)
        hp += equipment.weaponLeft.hp();
    if (equipment.weaponRight.category == ItemCategoryWeapon)
        hp += equipment.weaponRight.hp();
    if (equipment.chest.category == ItemCategoryChest)
        hp += equipment.chest.hp();
    if (equipment.feet.category == ItemCategoryFeet)
        hp += equipment.feet.hp();
    if (equipment.hands.category == ItemCategoryHands)
        hp += equipment.hands.hp();
    if (equipment.shoulder.category == ItemCategoryShoulder)
        hp += equipment.shoulder.hp();
    return hp;
}

int Creature::maxStunTime()
{
    if (m_entityData.hostilityType == HostilityPlayer)
        return PlayerStunDurationMs;
    return NpcBaseStunDurationMs - m_entityData.rarity * NpcRarityStunReductionMs / NpcRarityStunReductionSteps;
}

int Creature::nextAttackSkill()
{
    if (isCaster()) {
        if (m_entityData.equipment.weaponRight.id == WeaponWand)
            return m_entityData.specialization == SpecializationWater ? AbilityWandWaterDrop : AbilityWandFirebolt;
        if (m_entityData.equipment.weaponRight.id == WeaponStaff)
            return m_entityData.specialization == SpecializationWater ? AbilityWaterSwirl : AbilityFireSwirl;
        if (m_entityData.specialization == SpecializationWater) {
            if (m_entityData.currentAbility == AbilityBraceletWaterDrop &&
                m_entityData.timeSinceAbility < attackDuration() + ComboContinuationWindowMs)
                return AbilityWaterDrop;
            return AbilityBraceletWaterDrop;
        }
        if (m_entityData.currentAbility == AbilityBraceletFirebolt &&
            m_entityData.timeSinceAbility < attackDuration() + ComboContinuationWindowMs)
            return AbilityFirebolt;
        return AbilityBraceletFirebolt;
    }
    if (m_entityData.race == RaceSnoutBeetle)
        return AbilityShoot;
    if (m_entityData.equipment.weaponRight.category == ItemCategoryWeapon &&
        (m_entityData.equipment.weaponRight.id == WeaponBow || m_entityData.equipment.weaponRight.id == WeaponCrossbow))
        return AbilityShoot;
    if (m_entityData.equipment.weaponLeft.category == ItemCategoryWeapon &&
        (m_entityData.equipment.weaponLeft.id == WeaponBow || m_entityData.equipment.weaponLeft.id == WeaponCrossbow))
        return AbilityShoot;
    if (m_entityData.equipment.weaponRight.id == WeaponDagger) {
        if (m_entityData.currentAbility == AbilityRightDaggerPierce &&
            m_entityData.timeSinceAbility < attackDuration() + 200)
            return AbilityLeftDaggerPierce;
        return AbilityRightDaggerPierce;
    }
    if (m_entityData.equipment.weaponRight.id == WeaponFist ||
        m_entityData.equipment.weaponRight.category == ItemCategoryNone) {
        if (m_entityData.currentAbility == AbilityRightStab && m_entityData.timeSinceAbility < attackDuration() + 200)
            return AbilityLeftStab;
        return AbilityRightStab;
    }
    if (m_entityData.equipment.weaponRight.id == WeaponLongsword) {
        if (m_entityData.currentAbility == AbilityRightSweep && m_entityData.timeSinceAbility < attackDuration() + 200)
            return AbilityLeftSweep;
        return AbilityRightSweep;
    }
    if (m_entityData.equipment.weaponRight.category == ItemCategoryWeapon &&
        m_entityData.equipment.weaponRight.id == WeaponBoomerang)
        return AbilityThrowBoomerang;
    if (hasTwoHandedWeapon()) {
        if (m_entityData.currentAbility == AbilityTwoHandedSweep &&
            m_entityData.timeSinceAbility < attackDuration() + 200)
            return AbilityTwoHandedSwing;
        if (m_entityData.currentAbility == AbilityTwoHandedSwing &&
            m_entityData.timeSinceAbility < attackDuration() + 200)
            return AbilityTwoHandedThrust;
        return AbilityTwoHandedSweep;
    }
    if (m_entityData.equipment.weaponLeft.category == ItemCategoryWeapon &&
        m_entityData.equipment.weaponLeft.id == WeaponShield) {
        if (m_entityData.currentAbility == AbilityBlockJab && m_entityData.timeSinceAbility < attackDuration() + 200)
            return AbilityShieldAttack;
        return AbilityBlockJab;
    }
    if (m_entityData.appearance.appearanceFlags & (1 << AppearanceFlagBiteAttack))
        return AbilityBite;
    if (m_entityData.currentAbility == AbilityRightSlash && m_entityData.timeSinceAbility < attackDuration() + 200)
        return AbilityLeftSlash;
    return AbilityRightSlash;
}

int Creature::nextSpecialAttackSkill()
{
    if (m_entityData.equipment.weaponRight.category == ItemCategoryWeapon &&
        m_entityData.equipment.weaponRight.id == WeaponBow)
        return AbilityChargedShotRelease;
    if (isCaster()) {
        if (m_entityData.equipment.weaponRight.id == WeaponWand)
            return m_entityData.specialization == SpecializationWater ? AbilityWaterSalvo : AbilityFireSalvo;
        if (m_entityData.equipment.weaponRight.id == WeaponStaff)
            return m_entityData.specialization == SpecializationWater ? AbilityWaterVortex : AbilityFireVortex;
        return m_entityData.specialization == SpecializationWater ? AbilityWaterSplash : AbilityFireball;
    }
    if (m_entityData.equipment.weaponRight.category == ItemCategoryWeapon &&
        m_entityData.equipment.weaponRight.id == WeaponLongsword)
        return AbilityPerforate;
    if (m_entityData.equipment.weaponRight.isTwoHanded())
        return m_entityData.hitCombo % 2 ? AbilityTwoHandedSpin : AbilityTwoHandedSlam;
    return m_entityData.hitCombo % 2 ? Ability4 : Ability3;
}

// Walks the skill cooldowns without changing them.
void Creature::onHit()
{
    for (std::map<int, int>::iterator it = m_skillCooldowns.begin(); it != m_skillCooldowns.end(); ++it) {
    }
}

bool Creature::performAttack()
{
    if (m_entityData.rollTime != 0 || m_entityData.currentAbility == AbilityIntercept)
        return false;
    // In a combo the next attack may start during the current one's hold, except after the skills 30
    // and 32.
    int duration = attackDuration();
    if (m_entityData.hitCombo != 0 && m_entityData.currentAbility != AbilityFireSwirl &&
        m_entityData.currentAbility != AbilityWaterSwirl)
        duration -= holdTime();
    if (m_entityData.timeSinceAbility >= duration) {
        m_entityData.currentAbility = nextAttackSkill();
        m_entityData.timeSinceAbility = 0;
    }
    return true;
}

// A rogue of specialization 1 may start the skills 5, 17 and 20 while rolling (and keeps rolling);
// the skills 31, 33, 37, 46 and 95 skip their anticipation under buff 9.
bool Creature::performSpecialAttack()
{
    int duration = attackDuration();
    if (m_entityData.hitCombo != 0)
        duration -= holdTime();
    int skill = nextSpecialAttackSkill();
    bool usableWhileRolling = m_entityData.classType == ClassRogue &&
                              m_entityData.specialization == SpecializationNinja &&
                              (skill == AbilityAmbush || skill == AbilityPerforate || skill == AbilityKick);
    if (m_entityData.timeSinceAbility >= duration && (m_entityData.rollTime == 0 || usableWhileRolling)) {
        if (manaCost(skill, -1) > m_entityData.mp)
            return false;
        m_entityData.currentAbility = nextSpecialAttackSkill();
        m_entityData.timeSinceAbility = 0;
        if (m_entityData.currentAbility == AbilityFireball || m_entityData.currentAbility == AbilityFireSalvo ||
            m_entityData.currentAbility == AbilityFireRay || m_entityData.currentAbility == AbilityFireVortex ||
            m_entityData.currentAbility == AbilityWaterVortex) {
            if (findBuff(BuffInstantFire))
                m_entityData.timeSinceAbility = anticipationTime();
        }
        if (!usableWhileRolling)
            m_entityData.rollTime = 0;
        return true;
    }
    return false;
}

int Creature::power()
{
    return (1.0f - 1.0f / ((m_entityData.level - 1.0f) * 0.05f + 1.0f)) * 100.0f + 1.0f;
}

float itemPowerScale(float level, int rarity)
{
    // VC11's x86 powf is (float)pow((double)x, (double)y): the original calls the double pow.
    return powf(2.0f, (1.0f - 1.0f / ((level - 1.0f) * 0.05f + 1.0f)) * 3.0f) * powf(2.0f, 0.25f * rarity);
}

float itemDamageScale(float level, int rarity)
{
    // VC11's x86 powf is (float)pow((double)x, (double)y): the original calls the double pow.
    return powf(2.0f, (1.0f - 1.0f / ((level - 1.0f) * 0.05f + 1.0f)) * 3.0f) * powf(2.0f, 0.25f * rarity) /
           powf(2.0f, 3.0f);
}

// Enemy tint palette: flagged enemies are magenta; other creatures retain white modulation.
plasma::Vector<4, float> Creature::color()
{
    if ((m_entityData.appearance.appearanceFlags & (1 << AppearanceFlagTinted)) &&
        m_entityData.hostilityType == HostilityEnemy)
        return plasma::Vector<4, float>(0.8f, 0.0f, 0.5f, 1.0f);
    return plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f);
}

float Creature::regeneration()
{
    float rate = 0.0f;
    Equipment &equipment = m_entityData.equipment;
    if (equipment.weaponLeft.category == ItemCategoryWeapon)
        rate += equipment.weaponLeft.regeneration();
    if (equipment.weaponRight.category == ItemCategoryWeapon)
        rate += equipment.weaponRight.regeneration();
    if (equipment.chest.category == ItemCategoryChest)
        rate += equipment.chest.regeneration();
    if (equipment.feet.category == ItemCategoryFeet)
        rate += equipment.feet.regeneration();
    if (equipment.hands.category == ItemCategoryHands)
        rate += equipment.hands.regeneration();
    if (equipment.shoulder.category == ItemCategoryShoulder)
        rate += equipment.shoulder.regeneration();
    if (equipment.neck.category == ItemCategoryNeck)
        rate += equipment.neck.regeneration();
    if (equipment.ringLeft.category == ItemCategoryRing)
        rate += equipment.ringLeft.regeneration();
    if (equipment.ringRight.category == ItemCategoryRing)
        rate += equipment.ringRight.regeneration();
    return rate;
}

float levelFromBonus(float bonus)
{
    return (1.0f / (1.0f - bonus) - 1.0f) * 20.0f + 1.0f;
}

void Creature::reset()
{
    m_entityData.reset();
    if (m_behavior) {
        delete m_behavior;
        m_behavior = 0;
    }
    m_shownXp = 0;
    m_shownLevel = 1;
    m_petId = 0;
    m_opponentId = 0;
    m_companionId = 0;
    m_swingCount = 0;
    m_rollTime = 0;
    m_interactions.clear();
    m_aggro.clear();
    m_hitCounts.clear();
    m_skillCooldowns.clear();
    m_damageDealtTime = 0;
    m_damageDealt = 0;
    m_timeSinceDamageDealt = 0;
    unused13e0 = 0;
    m_hitFlash = 0;
    m_stealth = 0.0f;
    m_stamina = 1.0f;
    m_smoothingTime = 0;
    m_walkPhase = 0;
    m_walkAmplitude = 0;
    m_fallStartHeight = 0;
    m_leapDuration = 0;
    m_attackPower = 0;
    m_criticalStrike = 0;
    unused1404 = 0;
    m_inventory.m_inventoryTabs.clear();
    clearPath();
    m_interactionTarget = plasma::Vector<3, int>(-1, -1, 0);
    unused1470.clear();
    m_scheduleReached = 0;
    m_scheduleEntry = 0;
    m_pathStartBlock = plasma::Vector<3, int>(-1, -1, -1);
    m_pathRadius = 0.0f;
    m_schedule.clear();
    m_buffs.clear();
}

void Creature::EntityData::reset()
{
    level = 1;
    xp = 0;
    velocity = plasma::Vector<3, float>(0, 0, 0);
    acceleration = plasma::Vector<3, float>(0, 0, 0);
    retreat = plasma::Vector<3, float>(0, 0, 0);
    multipliers.hp = DefaultHealthMultiplier;
    multipliers.damage = 1;
    multipliers.attackSpeed = 1;
    currentAbility = AbilityNone;
    timeSinceAbility = 0;
    stunTime = -StunRecoveryDelayMs;
    actionLockTime = 0;
    slowedTime = 0;
    sprintTime = 0;
    revivalPenalty = 0;
    memset(&equipment, 0, sizeof(equipment));
    binaryToggles = BinaryToggles();
    interactionState = 0;
    buildingModel = 0;
    hitCombo = 0;
    timeSinceHit = 0;
    charge = 0;
    mp = 0;
    blockPower = 0;
    ownerId = 0;
    focusId = 0;
    rarity = ItemRarityWhite;
    finalHandsRotation = plasma::Vector<3, float>(0, 0, 0);
    initialHandsRotation = plasma::Vector<3, float>(0, 0, 0);
    spawnSlot = plasma::Vector<3, int>(-1, -1, 0);
    questZone = plasma::Vector<3, int>(-1, -1, 0);
    speechExpression = 0;
    classType = ClassNone;
    specialization = 0;
    appearance = Appearance();
    memset(name, 0, sizeof(name));
    memset(skillLevels, 0, sizeof(skillLevels));
    manaCubeCount = 0;
}

float Creature::resistance()
{
    float levelScale = (float)pow(2.0, (double)(levelBonus((float)m_entityData.level) * 3.0f));
    float scale = levelScale * (float)pow(2.0, (double)(m_entityData.rarity * 0.25f));
    float resistance = m_entityData.multipliers.resistance * scale;
    if (m_entityData.hostilityType == HostilityPlayer)
        resistance = (float)pow(2.0, 1.0) * levelScale * m_entityData.multipliers.resistance;
    if (m_entityData.appearance.appearanceFlags & (1 << AppearanceFlagUnarmored))
        resistance += scale;
    Equipment &equipment = m_entityData.equipment;
    if (equipment.chest.category == ItemCategoryChest)
        resistance += equipment.chest.resistance();
    if (equipment.feet.category == ItemCategoryFeet)
        resistance += equipment.feet.resistance();
    if (equipment.hands.category == ItemCategoryHands)
        resistance += equipment.hands.resistance();
    if (equipment.shoulder.category == ItemCategoryShoulder)
        resistance += equipment.shoulder.resistance();
    return resistance;
}

// Mount-specific rider offsets added to the seated pose; each race has a fixed vertical
// correction.
plasma::Vector<3, float> riderOffset(int race)
{
    switch (race) {
    case RaceTerrier:
    case RaceCat:
        return plasma::Vector<3, float>(0.0f, 0.0f, -0.9f);
    case RaceCrocodile:
        return plasma::Vector<3, float>(0.0f, 0.0f, 0.1f);
    case RaceDuckbill:
        return plasma::Vector<3, float>(0.0f, 0.0f, -0.3f);
    case RacePig:
        return plasma::Vector<3, float>(0.0f, 0.0f, -0.2f);
    case RaceBumblebee:
        return plasma::Vector<3, float>(0.0f, 0.0f, -0.35f);
    default:
        return plasma::Vector<3, float>(0.0f, 0.0f, 0.0f);
    }
}

float ridingSpeed(int points)
{
    if (points < 1)
        return 0.0f;
    return 1.0f - 1.0f / (points * 0.1f + 1.0f) + 1.0f;
}

// Replaces the first buff of these types; other types may stack.
void Creature::setBuff(const Buff &buff)
{
    if (buff.type == BuffBulwark || buff.type == BuffWarFrenzy || buff.type == BuffCamouflage ||
        buff.type == BuffManashield || buff.type == BuffTameness || buff.type == BuffInstantFire ||
        buff.type == BuffInstantShoot || buff.type == BuffGuaranteedCritical) {
        for (std::list<Buff>::iterator it = m_buffs.begin(); it != m_buffs.end(); ++it) {
            if (it->type == buff.type) {
                *it = buff;
                return;
            }
        }
    }
    m_buffs.push_back(buff);
}

plasma::Vector<3, Fixed> Creature::sightPosition()
{
    if (m_entityData.appearance.appearanceFlags & (1 << AppearanceFlagHeadAimOrigin)) {
        // Half the body's depth ahead and a third of its height up, turned with the creature.
        plasma::Matrix<float> turn;
        turn.identity();
        turn.rotateZ(m_entityData.rotation[2]);
        plasma::Vector<3, float> offset(0.0f, m_entityData.appearance.size[1] * 0.5f,
                                        m_entityData.appearance.size[2] * 0.35f);
        return m_entityData.position + plasma::Vector<3, Fixed>(turn * offset);
    }
    return m_entityData.position;
}

float Creature::spellPower()
{
    float damage = m_entityData.multipliers.damage * itemPowerScale(m_entityData.level, ItemRarityWhite);
    std::list<Item *> weapons = attackWeapons();
    if (weapons.empty()) {
        if (m_entityData.appearance.appearanceFlags & (1 << AppearanceFlagHandAttacks))
            damage += itemPowerScale(m_entityData.level, m_entityData.rarity) * 2.0f;
        if (m_entityData.appearance.appearanceFlags & (1 << AppearanceFlagBiteAttack))
            damage += itemPowerScale(m_entityData.level, m_entityData.rarity) * 8.0f;
    } else {
        for (std::list<Item *>::iterator it = weapons.begin(); it != weapons.end(); ++it)
            damage += (*it)->weaponDamage();
    }
    return damage;
}

// Clears motion, the current action and combat counters; keeps HP and the appearance, equipment
// and progression state.
void Creature::stopActions()
{
    m_fallStartHeight = 0;
    m_swingCount = 0;
    m_rollTime = 0;
    m_entityData.resetMotion();
    m_opponentId = 0;
    unused13e0 = 0;
    m_hitFlash = 0;
    m_stealth = 0.0f;
    m_stamina = 1.0f;
    m_aggro.clear();
    m_hitCounts.clear();
    m_skillCooldowns.clear();
    m_smoothingTime = 0;
    m_leapDuration = 0;
    m_walkPhase = 0;
    m_walkAmplitude = 0;
    m_attackPower = 0;
    m_criticalStrike = 0;
    unused1404 = 0;
    clearPath();
    m_interactionTarget = plasma::Vector<3, int>(-1, -1, 0);
    unused1470.clear();
    m_pathStartBlock = plasma::Vector<3, int>(-1, -1, -1);
    m_pathRadius = 0.0f;
}

void Creature::EntityData::resetMotion()
{
    velocity = plasma::Vector<3, float>(0, 0, 0);
    acceleration = plasma::Vector<3, float>(0, 0, 0);
    retreat = plasma::Vector<3, float>(0, 0, 0);
    currentAbility = AbilityNone;
    timeSinceAbility = 0;
    stunTime = -StunRecoveryDelayMs;
    actionLockTime = 0;
    binaryToggles = BinaryToggles();
    mp = 0;
    hitCombo = 0;
    timeSinceHit = 0;
    revivalPenalty = 0;
    finalHandsRotation = plasma::Vector<3, float>(0, 0, 0);
    initialHandsRotation = plasma::Vector<3, float>(0, 0, 0);
}

int Creature::strikeTime(int ability)
{
    if (ability < 0)
        ability = m_entityData.currentAbility;
    switch (ability) {
    case AbilityNone:
    case AbilityTeleport:
        return 0;
    case AbilityRetreat:
    case AbilityDashForward:
    case AbilityDashLeft:
    case AbilityDashRight:
    case AbilityShuriken:
        return RetreatGroupStrikeMs;
    case AbilitySmash:
        return SmashStrikeMs;
    case Ability3:
    case Ability4:
    case Ability62:
        return (int)(100.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityLeftDaggerPierce:
    case AbilityRightDaggerPierce:
        return (int)(150.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityRightStab:
    case AbilityLeftStab:
        return (int)(150.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityBlockJab:
        return BlockJabStrikeMs;
    case AbilityBlockStrike:
        return (int)(300.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityRightSlash:
    case AbilityLeftSlash:
    case AbilityShieldAttack:
        return (int)(200.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityTwoHandedSwing:
        return (int)(200.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityPerforate:
    case AbilityCrossedWeaponSweep:
    case AbilityMonsterChargeStrike:
    case AbilityAmbush:
    case AbilityTwoHandedSpin:
    case AbilityTwoHandedSlam:
        return (int)(400.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityKick:
    case AbilityRangerKick:
        return (int)(400.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityLeftSweep:
    case AbilityRightSweep:
    case AbilityOverheadChop:
        return (int)(200.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityFireSwirl:
    case AbilityWaterSwirl:
    case AbilityScream:
    case Ability93:
        return FireSwirlGroupStrikeMs;
    case AbilityBite:
        return (int)(300.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityShellBlock:
        return ShellBlockStrikeMs;
    case AbilityShellSpin:
    case AbilityCyclone:
        return ShellSpinGroupStrikeMs;
    case AbilityQuake:
        return QuakeStrikeMs;
    case AbilityHeadbutt:
    case AbilityHornCharge:
        return HeadbuttGroupStrikeMs;
    case AbilityFireBeam:
        return (int)(500.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityFireRay:
        return FireRayStrikeMs;
    case AbilityFireball:
    case AbilityWaterSplash:
        return (int)(100.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityWaterSalvo:
    case AbilityFireSalvo:
        return (int)(500.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilitySalvoRelease:
        return (int)(500.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityWandFirebolt:
    case AbilityFirebolt:
    case AbilityBraceletFirebolt:
    case AbilityWaterDrop:
    case AbilityBraceletWaterDrop:
    case AbilityWandWaterDrop:
        return (int)(300.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityFireVortex:
    case AbilityWaterVortex:
        return FireVortexGroupStrikeMs;
    case AbilityHealingStream:
        return (int)(1200.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilitySwirlStrike:
        return (int)(300.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityShoot:
        return (int)(50.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityChargedShotRelease:
        return (int)(50.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    case AbilityThrowBoomerang:
        return (int)(1200.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    default:
        return (int)(300.0f / (tempo() * m_entityData.multipliers.attackSpeed));
    }
}

float swimmingSpeed(int points)
{
    if (points < 1)
        return 0.5f;
    return (1.0f - 1.0f / (points * 0.1f + 1.0f)) * 0.5f + 0.5f;
}

float Creature::tempo()
{
    float tempo = 1.0f;
    if (m_entityData.hostilityType != HostilityPlayer)
        tempo = m_entityData.rarity * 0.0625f + 0.75f;
    if (m_entityData.classType == ClassWarrior && m_entityData.specialization == SpecializationBerserker)
        tempo += ((float)m_entityData.hitCombo / maxCombo() > 1.0f ? 1.0f : (float)m_entityData.hitCombo / maxCombo()) *
                 0.5f;
    if (m_entityData.classType == ClassMage && m_entityData.specialization == SpecializationWater)
        tempo += (float)m_entityData.hitCombo / maxCombo() > 1.0f ? 1.0f : (float)m_entityData.hitCombo / maxCombo();
    tempo += tempoBonus();
    if (findBuff(BuffTempoBoost)) {
        int points = m_entityData.skillLevels[SkillClassAbilityThird];
        if (points == 0 && m_entityData.hostilityType != HostilityPlayer)
            points = m_entityData.level / 2 + m_entityData.rarity + 1;
        tempo *= (1.0f - 1.0f / (points * 0.1f + 1.0f)) + 1.0f;
    }
    return tempo;
}

float Creature::tempoBonus()
{
    float bonus = itemDamageScale((float)m_entityData.level, ItemRarityWhite) * 0.1f;
    Equipment &equipment = m_entityData.equipment;
    if (equipment.weaponLeft.category == ItemCategoryWeapon)
        bonus += equipment.weaponLeft.tempo();
    if (equipment.weaponRight.category == ItemCategoryWeapon)
        bonus += equipment.weaponRight.tempo();
    if (equipment.chest.category == ItemCategoryChest)
        bonus += equipment.chest.tempo();
    if (equipment.feet.category == ItemCategoryFeet)
        bonus += equipment.feet.tempo();
    if (equipment.hands.category == ItemCategoryHands)
        bonus += equipment.hands.tempo();
    if (equipment.shoulder.category == ItemCategoryShoulder)
        bonus += equipment.shoulder.tempo();
    if (equipment.neck.category == ItemCategoryNeck)
        bonus += equipment.neck.tempo();
    if (equipment.ringLeft.category == ItemCategoryRing)
        bonus += equipment.ringLeft.tempo();
    if (equipment.ringRight.category == ItemCategoryRing)
        bonus += equipment.ringRight.tempo();
    return bonus;
}

void Creature::applyLevelUps(World *world)
{
    if (m_entityData.level > 0) {
        while (m_entityData.xp >= maxExperience()) {
            m_entityData.xp -= maxExperience();
            ++m_entityData.level;
            m_entityData.hp = maxHP();
        }
    }
}

}
