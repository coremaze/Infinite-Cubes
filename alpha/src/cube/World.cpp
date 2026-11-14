#include "cube/WorldDimensions.h"
// World manages zones, creatures, terrain and shared game state.
#include "cube/WorldBlocks.h"

#include <math.h>
#include <stdlib.h>
#include <sstream>

#include "cube/Creature.h"
#include "cube/CreatureIds.h"
#include "cube/Formula.h"
#include "cube/House.h"
#include "cube/Item.h"
#include "cube/ItemIds.h"
#include "cube/Noise.h"
#include "cube/RaceIds.h"
#include "cube/Region.h"
#include "cube/Serializer.h"
#include "cube/ServerUpdates.h"
#include "cube/SoundIds.h"
#include "cube/Spawn.h"
#include "cube/Sprite.h"
#include "cube/StatCurves.h"

namespace cube {

namespace {
const int DefaultWorldSeed = 123;
const int PetFoodSubtypeCount = 156;
}

World::World(IDirect3DDevice9 *device, bool client)
    : m_spriteManager(device),
      m_debugFlags(0),
      m_client(client),
      m_localPlayer(0),
      m_clock(0),
      m_time(12 * World::MillisecondsPerHour),
      m_day(0)
{
    setup(DefaultWorldSeed, "");

    // Dictionary keys indexed by the corresponding creature, item, object, skill or location ID.
    m_creatureKeys[RaceElfMale] = L"ElfMale";
    m_creatureKeys[RaceElfFemale] = L"ElfFemale";
    m_creatureKeys[RaceHumanMale] = L"HumanMale";
    m_creatureKeys[RaceHumanFemale] = L"HumanFemale";
    m_creatureKeys[RaceGoblinMale] = L"GoblinMale";
    m_creatureKeys[RaceGoblinFemale] = L"GoblinFemale";
    m_creatureKeys[RaceBullterrier] = L"Bullterrier";
    m_creatureKeys[RaceLizardmanMale] = L"LizardmanMale";
    m_creatureKeys[RaceLizardmanFemale] = L"LizardmanFemale";
    m_creatureKeys[RaceFrogmanMale] = L"FrogmanMale";
    m_creatureKeys[RaceFrogmanFemale] = L"FrogmanFemale";
    m_creatureKeys[RaceZombie] = L"Zombie";
    m_creatureKeys[RaceBandit] = L"Bandit";
    m_creatureKeys[RaceOldMan] = L"OldMan";
    m_creatureKeys[RaceWizard] = L"Wizard";
    m_creatureKeys[RaceTroll] = L"Troll";
    m_creatureKeys[RaceHellDemon] = L"HellDemon";
    m_creatureKeys[RaceGolem] = L"Golem";
    m_creatureKeys[RaceEmberGolem] = L"EmberGolem";
    m_creatureKeys[RaceSnowGolem] = L"SnowGolem";
    m_creatureKeys[RaceWerewolf] = L"Werewolf";
    m_creatureKeys[RaceDarkTroll] = L"DarkTroll";
    m_creatureKeys[RaceYeti] = L"Yeti";
    m_creatureKeys[RaceOgre] = L"Ogre";
    m_creatureKeys[RaceRockling] = L"Rockling";
    m_creatureKeys[RaceVampire] = L"Vampire";
    m_creatureKeys[RaceSpitter] = L"Spitter";
    m_creatureKeys[RaceSpikeCreature] = L"SpikeCreature";
    m_creatureKeys[RaceAnubis] = L"Anubis";
    m_creatureKeys[RaceHorus] = L"Horus";
    m_creatureKeys[RaceJester] = L"Jester";
    m_creatureKeys[RaceSpectrino] = L"Spectrino";
    m_creatureKeys[RaceDjinn] = L"Djinn";
    m_creatureKeys[RaceMinotaur] = L"Minotaur";
    m_creatureKeys[RaceImp] = L"Imp";
    m_creatureKeys[RaceDragon] = L"Dragon";
    m_creatureKeys[RaceLich] = L"Lich";
    m_creatureKeys[RaceDwarfMale] = L"DwarfMale";
    m_creatureKeys[RaceDwarfFemale] = L"DwarfFemale";
    m_creatureKeys[RaceHornet] = L"Hornet";
    m_creatureKeys[RaceInsectGuard] = L"InsectGuard";
    m_creatureKeys[RaceGinseng] = L"Ginseng";
    m_creatureKeys[RaceFly] = L"Fly";
    m_creatureKeys[RaceMidge] = L"Midge";
    m_creatureKeys[RaceMosquito] = L"Mosquito";
    m_creatureKeys[RaceSeagull] = L"Seagull";
    m_creatureKeys[RaceRadishCreature] = L"RadishCreature";
    m_creatureKeys[RacePlantCreature] = L"PlantCreature";
    m_creatureKeys[RaceOnionling] = L"Onionling";
    m_creatureKeys[RaceDesertOnionling] = L"DesertOnionling";
    m_creatureKeys[RaceDevourer] = L"Devourer";
    m_creatureKeys[RaceCrab] = L"Crab";
    m_creatureKeys[RaceSeaCrab] = L"SeaCrab";
    m_creatureKeys[RaceBarkBeetle] = L"BarkBeetle";
    m_creatureKeys[RaceFireBeetle] = L"FireBeetle";
    m_creatureKeys[RaceSnoutBeetle] = L"SnoutBeetle";
    m_creatureKeys[RaceLemonBeetle] = L"LemonBeetle";
    m_creatureKeys[RaceLemonFish] = L"LemonFish";
    m_creatureKeys[RaceSapphireFish] = L"SapphireFish";
    m_creatureKeys[RaceShark] = L"Shark";
    m_creatureKeys[RaceLanternFish] = L"LanternFish";
    m_creatureKeys[RaceMawFish] = L"MawFish";
    m_creatureKeys[RacePiranha] = L"Piranha";
    m_creatureKeys[RaceBlowfish] = L"Blowfish";
    m_creatureKeys[RaceSeahorse] = L"Seahorse";
    m_creatureKeys[RaceFrog] = L"Frog";
    m_creatureKeys[RaceMole] = L"Mole";
    m_creatureKeys[RaceBiter] = L"Biter";
    m_creatureKeys[RaceSquirrel] = L"Squirrel";
    m_creatureKeys[RaceRaccoon] = L"Raccoon";
    m_creatureKeys[RaceDuckbill] = L"Duckbill";
    m_creatureKeys[RaceCrocodile] = L"Crocodile";
    m_creatureKeys[RaceOwl] = L"Owl";
    m_creatureKeys[RacePenguin] = L"Penguin";
    m_creatureKeys[RaceCrow] = L"Crow";
    m_creatureKeys[RaceParrot] = L"Parrot";
    m_creatureKeys[RaceSaurian] = L"Saurian";
    m_creatureKeys[RaceRuneGiant] = L"RuneGiant";
    m_creatureKeys[RaceCyclops] = L"Cyclops";
    m_creatureKeys[RaceMammoth] = L"Mammoth";
    m_creatureKeys[RaceWitch] = L"Witch";
    m_creatureKeys[RaceGnobold] = L"Gnobold";
    m_creatureKeys[RaceInsectoid] = L"Insectoid";
    m_creatureKeys[RaceGnoll] = L"Gnoll";
    m_creatureKeys[RacePolarGnoll] = L"PolarGnoll";
    m_creatureKeys[RaceMermaid] = L"Mermaid";
    m_creatureKeys[RaceMerman] = L"Merman";
    m_creatureKeys[RaceBat] = L"Bat";
    m_creatureKeys[RaceOrcMale] = L"OrcMale";
    m_creatureKeys[RaceOrcFemale] = L"OrcFemale";
    m_creatureKeys[RaceGreenSlime] = L"GreenSlime";
    m_creatureKeys[RacePinkSlime] = L"PinkSlime";
    m_creatureKeys[RaceYellowSlime] = L"YellowSlime";
    m_creatureKeys[RaceBlueSlime] = L"BlueSlime";
    m_creatureKeys[RaceFrightener] = L"Frightener";
    m_creatureKeys[RaceSandHorror] = L"SandHorror";
    m_creatureKeys[RaceBush] = L"Bush";
    m_creatureKeys[RaceSnowBush] = L"SnowBush";
    m_creatureKeys[RaceSnowBerryBush] = L"SnowBerryBush";
    m_creatureKeys[RaceThornTree] = L"ThornTree";
    m_creatureKeys[RaceCactus] = L"Cactus";
    m_creatureKeys[RaceDummy] = L"Dummy";
    m_creatureKeys[RaceAim] = L"Aim";
    m_creatureKeys[RaceCottonPlant] = L"CottonPlant";
    m_creatureKeys[RaceScrub] = L"Scrub";
    m_creatureKeys[RaceCobwebScrub] = L"CobwebScrub";
    m_creatureKeys[RaceFireScrub] = L"FireScrub";
    m_creatureKeys[RaceBomb] = L"Bomb";
    m_creatureKeys[RaceUndeadMale] = L"UndeadMale";
    m_creatureKeys[RaceUndeadFemale] = L"UndeadFemale";
    m_creatureKeys[RaceSkeleton] = L"Skeleton";
    m_creatureKeys[RacePlainRunner] = L"PlainRunner";
    m_creatureKeys[RaceLeafRunner] = L"LeafRunner";
    m_creatureKeys[RaceSnowRunner] = L"SnowRunner";
    m_creatureKeys[RaceDesertRunner] = L"DesertRunner";
    m_creatureKeys[RacePeacock] = L"Peacock";
    m_creatureKeys[RaceBunny] = L"Bunny";
    m_creatureKeys[RacePorcupine] = L"Porcupine";
    m_creatureKeys[RaceCollie] = L"Collie";
    m_creatureKeys[RaceShepherdDog] = L"ShepherdDog";
    m_creatureKeys[RaceAlpaca] = L"Alpaca";
    m_creatureKeys[RaceBrownAlpaca] = L"BrownAlpaca";
    m_creatureKeys[RaceSkullBull] = L"SkullBull";
    m_creatureKeys[RaceEgg] = L"Egg";
    m_creatureKeys[RaceTurtle] = L"Turtle";
    m_creatureKeys[RaceTerrier] = L"Terrier";
    m_creatureKeys[RaceScottishTerrier] = L"ScottishTerrier";
    m_creatureKeys[RacePanther] = L"Panther";
    m_creatureKeys[RaceCat] = L"Cat";
    m_creatureKeys[RaceBrownCat] = L"BrownCat";
    m_creatureKeys[RaceWhiteCat] = L"WhiteCat";
    m_creatureKeys[RaceWolf] = L"Wolf";
    m_creatureKeys[RacePig] = L"Pig";
    m_creatureKeys[RaceSheep] = L"Sheep";
    m_creatureKeys[RaceHorse] = L"Horse";
    m_creatureKeys[RaceCow] = L"Cow";
    m_creatureKeys[RaceCamel] = L"Camel";
    m_creatureKeys[RaceMonkey] = L"Monkey";
    m_creatureKeys[RaceChicken] = L"Chicken";
    m_creatureKeys[RaceMosquito] = L"Mosquito";
    m_creatureKeys[RacePorcupine] = L"Porcupine";
    m_creatureKeys[RaceVase] = L"Vase";
    m_creatureKeys[RaceIronDeposit] = L"IronDeposit";
    m_creatureKeys[RaceGoldDeposit] = L"GoldDeposit";
    m_creatureKeys[RaceEmeraldDeposit] = L"EmeraldDeposit";
    m_creatureKeys[RaceSapphireDeposit] = L"SapphireDeposit";
    m_creatureKeys[RaceRubyDeposit] = L"RubyDeposit";
    m_creatureKeys[RaceDiamondDeposit] = L"DiamondDeposit";
    m_creatureKeys[RaceSilverDeposit] = L"SilverDeposit";
    m_creatureKeys[RaceSandstoneDeposit] = L"SandstoneDeposit";
    m_creatureKeys[RaceIceCrystalDeposit] = L"IceCrystalDeposit";
    m_creatureKeys[RaceScarecrow] = L"Scarecrow";
    m_creatureKeys[RaceNomadMale] = L"NomadMale";
    m_creatureKeys[RaceNomadFemale] = L"NomadFemale";
    m_creatureKeys[RaceBumblebee] = L"Bumblebee";
    m_creatureKeys[RaceKoala] = L"Koala";

    // Dictionary keys indexed by the corresponding creature, item, object, skill or location ID.
    m_itemKeys[std::make_pair(ItemCategoryFeet, int(FeetBoots))] = L"Boots";
    m_itemKeys[std::make_pair(ItemCategoryHands, int(HandsGloves))] = L"Gloves";
    m_itemKeys[std::make_pair(ItemCategoryChest, int(ChestChestArmor))] = L"ChestArmor";
    m_itemKeys[std::make_pair(ItemCategoryShoulder, int(ShoulderShoulderArmor))] = L"ShoulderArmor";
    m_itemKeys[std::make_pair(ItemCategoryPet, 0)] = L"Pet";
    m_itemKeys[std::make_pair(ItemCategoryNeck, int(NeckAmulet))] = L"Amulet";
    m_itemKeys[std::make_pair(ItemCategoryRing, int(RingRing))] = L"Ring";
    m_itemKeys[std::make_pair(ItemCategoryFormula, int(FormulaFormula))] = L"Formula";
    m_itemKeys[std::make_pair(ItemCategoryWeapon, int(WeaponSword))] = L"Sword";
    m_itemKeys[std::make_pair(ItemCategoryWeapon, int(WeaponDagger))] = L"Dagger";
    m_itemKeys[std::make_pair(ItemCategoryWeapon, int(WeaponFist))] = L"Fist";
    m_itemKeys[std::make_pair(ItemCategoryWeapon, int(WeaponMace))] = L"Mace";
    m_itemKeys[std::make_pair(ItemCategoryWeapon, int(WeaponAxe))] = L"Axe";
    m_itemKeys[std::make_pair(ItemCategoryWeapon, int(WeaponBow))] = L"Bow";
    m_itemKeys[std::make_pair(ItemCategoryWeapon, int(WeaponCrossbow))] = L"Crossbow";
    m_itemKeys[std::make_pair(ItemCategoryWeapon, int(WeaponBoomerang))] = L"Boomerang";
    m_itemKeys[std::make_pair(ItemCategoryWeapon, int(WeaponArrows))] = L"Arrows";
    m_itemKeys[std::make_pair(ItemCategoryWeapon, int(WeaponArrow))] = L"Arrow";
    m_itemKeys[std::make_pair(ItemCategoryWeapon, int(WeaponTorch))] = L"Torch";
    m_itemKeys[std::make_pair(ItemCategoryWeapon, int(WeaponStaff))] = L"Staff";
    m_itemKeys[std::make_pair(ItemCategoryWeapon, int(WeaponWand))] = L"Wand";
    m_itemKeys[std::make_pair(ItemCategoryWeapon, int(WeaponBracelet))] = L"Bracelet";
    m_itemKeys[std::make_pair(ItemCategoryWeapon, int(WeaponShield))] = L"Shield";
    m_itemKeys[std::make_pair(ItemCategoryWeapon, int(WeaponGreatsword))] = L"Greatsword";
    m_itemKeys[std::make_pair(ItemCategoryWeapon, int(WeaponGreataxe))] = L"Greataxe";
    m_itemKeys[std::make_pair(ItemCategoryWeapon, int(WeaponGreatmace))] = L"Greatmace";
    m_itemKeys[std::make_pair(ItemCategoryWeapon, int(WeaponLongsword))] = L"Longsword";
    m_itemKeys[std::make_pair(ItemCategoryCoin, int(CoinCoin))] = L"Coin";
    m_itemKeys[std::make_pair(ItemCategoryPlatinumCoin, int(PlatinumCoinPlatinumCoin))] = L"PlatinumCoin";
    m_itemKeys[std::make_pair(ItemCategoryCandle, int(CandleItemCandle))] = L"Candle";
    m_itemKeys[std::make_pair(ItemCategoryCandle, 1)] = L"Candle";
    m_itemKeys[std::make_pair(ItemCategorySpecial, int(SpecialItemHangGlider))] = L"HangGlider";
    m_itemKeys[std::make_pair(ItemCategorySpecial, int(SpecialItemBoat))] = L"Boat";
    m_itemKeys[std::make_pair(ItemCategoryLamp, int(LampLamp))] = L"Lamp";
    m_itemKeys[std::make_pair(ItemCategoryManaCube, int(ManaCubeManaCube))] = L"ManaCube";
    // Every pet food subtype is bait unless named below.
    for (int subtype = 0; subtype < PetFoodSubtypeCount; subtype++)
        m_itemKeys[std::make_pair(ItemCategoryPetFood, subtype)] = L"Bait";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodBlueJelly))] = L"BlueJelly";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodGreenJelly))] = L"GreenJelly";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodPinkJelly))] = L"PinkJelly";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodYellowJelly))] = L"YellowJelly";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodCarrot))] = L"Carrot";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodPumpkinMash))] = L"PumpkinMash";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodCandy))] = L"Candy";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodLollipop))] = L"Lollipop";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodSoftice))] = L"Softice";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodChocolateDonut))] = L"ChocolateDonut";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodCottonCandy))] = L"CottonCandy";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodPopcorn))] = L"Popcorn";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodCerealBar))] = L"CerealBar";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodStrawberryCake))] = L"StrawberryCake";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodChocolateCake))] = L"ChocolateCake";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodChocolateCupcake))] = L"ChocolateCupcake";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodVanillaCupcake))] = L"VanillaCupcake";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodBananaSplit))] = L"BananaSplit";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodCroissant))] = L"Croissant";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodBread))] = L"Bread";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodLolly))] = L"Lolly";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodLemonTart))] = L"LemonTart";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodChocolateCookie))] = L"ChocolateCookie";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodBubbleGum))] = L"BubbleGum";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodLicoriceCandy))] = L"LicoriceCandy";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodCinnamonRole))] = L"CinnamonRole";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodAppleRing))] = L"AppleRing";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodWaffle))] = L"Waffle";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodWaterIce))] = L"WaterIce";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodDateCookie))] = L"DateCookie";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodCandiedApple))] = L"CandiedApple";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodStrawberryCocktail))] = L"StrawberryCocktail";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodMilkChocolateBar))] = L"MilkChocolateBar";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodCaramelChocolateBar))] = L"CaramelChocolateBar";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodMintChocolateBar))] = L"MintChocolateBar";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodWhiteChocolateBar))] = L"WhiteChocolateBar";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodSugarCandy))] = L"SugarCandy";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodBlackberryMarmelade))] = L"BlackberryMarmelade";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodSaltedCaramel))] = L"SaltedCaramel";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodGingerTartlet))] = L"GingerTartlet";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodMangoJuice))] = L"MangoJuice";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodFruitBasket))] = L"FruitBasket";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodMelonIceCream))] = L"MelonIceCream";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodBloodOrangeJuice))] = L"BloodOrangeJuice";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodPancakes))] = L"Pancakes";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodCurry))] = L"Curry";
    m_itemKeys[std::make_pair(ItemCategoryPetFood, int(PetFoodBiscuitRole))] = L"BiscuitRole";
    m_itemKeys[std::make_pair(ItemCategoryConsumable, int(ConsumableLifePotion))] = L"LifePotion";
    m_itemKeys[std::make_pair(ItemCategoryConsumable, int(ConsumableCactusPotion))] = L"CactusPotion";
    m_itemKeys[std::make_pair(ItemCategoryConsumable, int(ConsumableManaPotion))] = L"ManaPotion";
    m_itemKeys[std::make_pair(ItemCategoryConsumable, int(ConsumableGinsengSoup))] = L"GinsengSoup";
    m_itemKeys[std::make_pair(ItemCategoryConsumable, int(ConsumableSnowBerryMash))] = L"SnowBerryMash";
    m_itemKeys[std::make_pair(ItemCategoryConsumable, int(ConsumableMushroomSpit))] = L"MushroomSpit";
    m_itemKeys[std::make_pair(ItemCategoryConsumable, int(ConsumablePineappleSlice))] = L"PineappleSlice";
    m_itemKeys[std::make_pair(ItemCategoryConsumable, int(ConsumablePumpkinMuffin))] = L"PumpkinMuffin";
    m_itemKeys[std::make_pair(ItemCategoryConsumable, int(ConsumableCookie))] = L"Cookie";
    m_itemKeys[std::make_pair(ItemCategoryConsumable, int(ConsumableBomb))] = L"Bomb";
    m_itemKeys[std::make_pair(ItemCategoryMaterial, int(MaterialItemNugget))] = L"Nugget";
    m_itemKeys[std::make_pair(ItemCategoryMaterial, int(MaterialItemLog))] = L"Log";
    m_itemKeys[std::make_pair(ItemCategoryMaterial, int(MaterialItemFeather))] = L"Feather";
    m_itemKeys[std::make_pair(ItemCategoryMaterial, int(MaterialItemHorn))] = L"Horn";
    m_itemKeys[std::make_pair(ItemCategoryMaterial, int(MaterialItemClaw))] = L"Claw";
    m_itemKeys[std::make_pair(ItemCategoryMaterial, int(MaterialItemFiber))] = L"Fiber";
    m_itemKeys[std::make_pair(ItemCategoryMaterial, int(MaterialItemHair))] = L"Hair";
    m_itemKeys[std::make_pair(ItemCategoryMaterial, int(MaterialItemCrystal))] = L"Crystal";
    m_itemKeys[std::make_pair(ItemCategoryMaterial, int(MaterialItemYarn))] = L"Yarn";
    m_itemKeys[std::make_pair(ItemCategoryMaterial, int(MaterialItemCobweb))] = L"Cobweb";
    m_itemKeys[std::make_pair(ItemCategoryMaterial, int(MaterialItemCube))] = L"Cube";
    m_itemKeys[std::make_pair(ItemCategoryMaterial, int(MaterialItemCapsule))] = L"Capsule";
    m_itemKeys[std::make_pair(ItemCategoryMaterial, int(MaterialItemOrb))] = L"Orb";
    m_itemKeys[std::make_pair(ItemCategoryMaterial, int(MaterialItemFlask))] = L"Flask";
    m_itemKeys[std::make_pair(ItemCategoryMaterial, int(MaterialItemSpirit))] = L"Spirit";
    m_itemKeys[std::make_pair(ItemCategoryMaterial, int(MaterialItemMushroom))] = L"Mushroom";
    m_itemKeys[std::make_pair(ItemCategoryMaterial, int(MaterialItemShimmerMushroom))] = L"ShimmerMushroom";
    m_itemKeys[std::make_pair(ItemCategoryMaterial, int(MaterialItemGinsengRoot))] = L"GinsengRoot";
    m_itemKeys[std::make_pair(ItemCategoryMaterial, int(MaterialItemPumpkin))] = L"Pumpkin";
    m_itemKeys[std::make_pair(ItemCategoryMaterial, int(MaterialItemPineapple))] = L"Pineapple";
    m_itemKeys[std::make_pair(ItemCategoryMaterial, int(MaterialItemHeartflower))] = L"Heartflower";
    m_itemKeys[std::make_pair(ItemCategoryMaterial, int(MaterialItemPricklyPear))] = L"PricklyPear";
    m_itemKeys[std::make_pair(ItemCategoryMaterial, int(MaterialItemFrozenHeartflower))] = L"FrozenHeartflower";
    m_itemKeys[std::make_pair(ItemCategoryMaterial, int(MaterialItemSoulflower))] = L"Soulflower";
    m_itemKeys[std::make_pair(ItemCategoryMaterial, int(MaterialItemRadishSlice))] = L"RadishSlice";
    m_itemKeys[std::make_pair(ItemCategoryMaterial, int(MaterialItemOnionSlice))] = L"OnionSlice";
    m_itemKeys[std::make_pair(ItemCategoryMaterial, int(MaterialItemWaterFlask))] = L"WaterFlask";
    m_itemKeys[std::make_pair(ItemCategoryMaterial, int(MaterialItemSnowBerry))] = L"SnowBerry";
    m_itemKeys[std::make_pair(ItemCategoryBeak, int(BeakBeak))] = L"Beak";
    m_itemKeys[std::make_pair(ItemCategoryQuestItem, int(QuestItemAmulet1))] = L"Amulet1";
    m_itemKeys[std::make_pair(ItemCategoryQuestItem, int(QuestItemAmulet2))] = L"Amulet2";
    m_itemKeys[std::make_pair(ItemCategoryQuestItem, int(QuestItemJewelCase))] = L"JewelCase";
    m_itemKeys[std::make_pair(ItemCategoryQuestItem, int(QuestItemKey))] = L"Key";
    m_itemKeys[std::make_pair(ItemCategoryQuestItem, int(QuestItemMedicine))] = L"Medicine";
    m_itemKeys[std::make_pair(ItemCategoryQuestItem, int(QuestItemAntivenom))] = L"Antivenom";
    m_itemKeys[std::make_pair(ItemCategoryQuestItem, int(QuestItemBandAid))] = L"BandAid";
    m_itemKeys[std::make_pair(ItemCategoryQuestItem, int(QuestItemCrutch))] = L"Crutch";
    m_itemKeys[std::make_pair(ItemCategoryQuestItem, int(QuestItemBandage))] = L"Bandage";
    m_itemKeys[std::make_pair(ItemCategoryQuestItem, int(QuestItemSalve))] = L"Salve";
    m_itemKeys[std::make_pair(ItemCategoryLeftovers, int(LeftoversLeftovers))] = L"Leftovers";
    m_itemKeys[std::make_pair(ItemCategoryPainting, int(PaintingPainting))] = L"Painting";

    // Dictionary keys indexed by the corresponding creature, item, object, skill or location ID.
    m_staticKeys[StaticObjectStatue] = L"Statue";
    m_staticKeys[StaticObjectDoor] = L"Door";
    m_staticKeys[StaticObjectBigDoor] = L"BigDoor";
    m_staticKeys[StaticObjectWindow] = L"Window";
    m_staticKeys[StaticObjectCastleWindow] = L"CastleWindow";
    m_staticKeys[StaticObjectGate] = L"Gate";
    m_staticKeys[StaticObjectFireTrap] = L"FireTrap";
    m_staticKeys[StaticObjectSpikeTrap] = L"SpikeTrap";
    m_staticKeys[StaticObjectStompTrap] = L"StompTrap";
    m_staticKeys[StaticObjectLever] = L"Lever";
    m_staticKeys[StaticObjectChest] = L"Chest";
    m_staticKeys[StaticObjectTable] = L"Table";
    m_staticKeys[StaticObjectTable14] = L"Table";
    m_staticKeys[StaticObjectStoneTable] = L"Table";
    m_staticKeys[StaticObjectStool] = L"Stool";
    m_staticKeys[StaticObjectStoneStool] = L"Stool";
    m_staticKeys[StaticObjectSandstoneStool] = L"Stool";
    m_staticKeys[StaticObjectBench] = L"Bench";
    m_staticKeys[StaticObjectBed] = L"Bed";
    m_staticKeys[StaticObjectBedTable] = L"BedTable";
    m_staticKeys[StaticObjectMarketStand1] = L"MarketStand1";
    m_staticKeys[StaticObjectMarketStand2] = L"MarketStand2";
    m_staticKeys[StaticObjectMarketStand3] = L"MarketStand3";
    m_staticKeys[StaticObjectBarrel] = L"Barrel";
    m_staticKeys[StaticObjectCrate] = L"Crate";
    m_staticKeys[StaticObjectOpenCrate] = L"OpenCrate";
    m_staticKeys[StaticObjectSack] = L"Sack";
    m_staticKeys[StaticObjectShelter] = L"Shelter";
    m_staticKeys[StaticObjectCupboard] = L"Cupboard";
    m_staticKeys[StaticObjectDesktop] = L"Desktop";
    m_staticKeys[StaticObjectCounter] = L"Counter";
    m_staticKeys[StaticObjectShelf1] = L"Shelf1";
    m_staticKeys[StaticObjectShelf2] = L"Shelf2";
    m_staticKeys[StaticObjectShelf3] = L"Shelf3";
    m_staticKeys[StaticObjectCorpse] = L"Corpse";
    m_staticKeys[StaticObjectRuneStone] = L"RuneStone";
    m_staticKeys[StaticObjectArtifact] = L"Artifact";
    m_staticKeys[StaticObjectFlowerBox1] = L"FlowerBox1";
    m_staticKeys[StaticObjectFlowerBox2] = L"FlowerBox2";
    m_staticKeys[StaticObjectFlowerBox3] = L"FlowerBox3";
    m_staticKeys[StaticObjectStreetLight] = L"StreetLight";
    m_staticKeys[StaticObjectFireStreetLight] = L"FireStreetLight";
    m_staticKeys[StaticObjectFence1] = L"Fence1";
    m_staticKeys[StaticObjectFence2] = L"Fence2";
    m_staticKeys[StaticObjectFence3] = L"Fence3";
    m_staticKeys[StaticObjectFence4] = L"Fence4";
    m_staticKeys[StaticObjectVase1] = L"Vase1";
    m_staticKeys[StaticObjectVase2] = L"Vase2";
    m_staticKeys[StaticObjectVase3] = L"Vase3";
    m_staticKeys[StaticObjectVase4] = L"Vase4";
    m_staticKeys[StaticObjectVase5] = L"Vase5";
    m_staticKeys[StaticObjectVase6] = L"Vase6";
    m_staticKeys[StaticObjectVase7] = L"Vase7";
    m_staticKeys[StaticObjectVase8] = L"Vase8";
    m_staticKeys[StaticObjectVase9] = L"Vase9";
    m_staticKeys[StaticObjectCampfire] = L"Campfire";
    m_staticKeys[StaticObjectTent] = L"Tent";
    m_staticKeys[StaticObjectBeachUmbrella] = L"BeachUmbrella";
    m_staticKeys[StaticObjectBeachTowel] = L"BeachTowel";
    m_staticKeys[StaticObjectSleepingMat] = L"SleepingMat";
    m_staticKeys[StaticObjectFurnace] = L"Furnace";
    m_staticKeys[StaticObjectAnvil] = L"Anvil";
    m_staticKeys[StaticObjectSpinningWheel] = L"SpinningWheel";
    m_staticKeys[StaticObjectLoom] = L"Loom";
    m_staticKeys[StaticObjectSaw] = L"SawBench";
    m_staticKeys[StaticObjectWorkbench] = L"Workbench";
    m_staticKeys[StaticObjectCustomizationBench] = L"CustomizationBench";

    // Dictionary keys indexed by the corresponding creature, item, object, skill or location ID.
    m_skillKeys[SkillClassAbilityFirst] = L"SkillAbility1";
    m_skillKeys[SkillClassAbilitySecond] = L"SkillAbility2";
    m_skillKeys[SkillClassAbilityThird] = L"SkillAbility3";
    m_skillKeys[SkillClassAbilityFourth] = L"SkillAbility4";
    m_skillKeys[SkillClassAbilityFifth] = L"SkillAbility5";
    m_skillKeys[SkillPetTaming] = L"SkillPetTaming";
    m_skillKeys[SkillPetRiding] = L"SkillPetRiding";
    m_skillKeys[SkillClimbing] = L"SkillClimbing";
    m_skillKeys[SkillHangGliding] = L"SkillHangGliding";
    m_skillKeys[SkillSwimming] = L"SkillSwimming";
    m_skillKeys[SkillBoatDriving] = L"SkillBoatDriving";

    // Dictionary keys indexed by the corresponding creature, item, object, skill or location ID.
    m_abilityKeys[AbilitySmash] = L"AbilitySmash";
    m_abilityKeys[AbilityCyclone] = L"AbilityCyclone";
    m_abilityKeys[AbilityBulwark] = L"AbilityBulwark";
    m_abilityKeys[AbilityWarFrenzy] = L"AbilityWarFrenzy";
    m_abilityKeys[AbilityRangerKick] = L"AbilityRangerKick";
    m_abilityKeys[AbilityRetreat] = L"AbilityRetreat";
    m_abilityKeys[AbilityAim] = L"AbilityAim";
    m_abilityKeys[AbilitySwiftness] = L"AbilitySwiftness";
    m_abilityKeys[AbilityFireExplosion] = L"AbilityFireExplosion";
    m_abilityKeys[AbilityManaShield] = L"AbilityManaShield";
    m_abilityKeys[AbilityTeleport] = L"AbilityTeleport";
    m_abilityKeys[AbilityHealingStream] = L"AbilityHealingStream";
    m_abilityKeys[AbilityIntercept] = L"AbilityIntercept";
    m_abilityKeys[AbilityShuriken] = L"AbilityShuriken";
    m_abilityKeys[AbilityCamouflage] = L"AbilityCamouflage";
    m_abilityKeys[AbilitySneak] = L"AbilitySneak";

    // Every style of these kinds has the kind's name. Dictionary keys indexed by the corresponding
    // creature, item, object, skill or location ID.
    for (int style = 0; style < 10; style++) {
        m_settlementKeys[std::make_pair(Region::Record::SceneryCity, style)] = L"Village";
        m_settlementKeys[std::make_pair(Region::Record::SceneryMountain, style)] = L"Mountain";
        m_settlementKeys[std::make_pair(Region::Record::SceneryForest, style)] = L"Forest";
        m_settlementKeys[std::make_pair(Region::Record::SceneryLake, style)] = L"Lake";
        m_settlementKeys[std::make_pair(Region::Record::SceneryCanyon, style)] = L"Canyon";
        m_settlementKeys[std::make_pair(Region::Record::SceneryRock, style)] = L"Rock";
        m_settlementKeys[std::make_pair(Region::Record::SceneryTree, style)] = L"Tree";
        m_settlementKeys[std::make_pair(Region::Record::SceneryValley, style)] = L"Valley";
        m_settlementKeys[std::make_pair(Region::Record::SceneryCrater, style)] = L"Crater";
        m_settlementKeys[std::make_pair(Region::Record::SceneryPeak, style)] = L"Peak";
        m_settlementKeys[std::make_pair(Region::Record::SceneryIsland, style)] = L"Island";
    }
    m_settlementKeys[std::make_pair(Region::Record::SceneryRuins, 0)] = L"Ruins";
    m_settlementKeys[std::make_pair(Region::Record::SceneryRuins, 1)] = L"Ruins";
    m_settlementKeys[std::make_pair(Region::Record::SceneryRuins, 2)] = L"Ruins";
    m_settlementKeys[std::make_pair(Region::Record::SceneryRuins, 3)] = L"Ruins";
    m_settlementKeys[std::make_pair(Region::Record::SceneryRuins, int(RuinGravesite))] = L"Gravesite";
    m_settlementKeys[std::make_pair(Region::Record::SceneryCastle, int(Region::Record::DungeonCastle))] = L"Castle";
    m_settlementKeys[std::make_pair(Region::Record::SceneryCastle, int(Region::Record::DungeonRuins))] = L"Ruins";
    m_settlementKeys[std::make_pair(Region::Record::SceneryCastle, int(Region::Record::DungeonCatacombs))] =
        L"Catacombs";
    m_settlementKeys[std::make_pair(Region::Record::SceneryCastle, int(Region::Record::DungeonPalace))] = L"Palace";
    m_settlementKeys[std::make_pair(Region::Record::SceneryCastle, int(Region::Record::DungeonTemple))] = L"Temple";
    m_settlementKeys[std::make_pair(Region::Record::SceneryCastle, int(Region::Record::DungeonPyramid))] = L"Pyramid";
    m_settlementKeys[std::make_pair(Region::Record::SceneryCave, 0)] = L"Cave";
    m_settlementKeys[std::make_pair(Region::Record::SceneryPortal, 0)] = L"Portal";

    // Dictionary keys indexed by the corresponding creature, item, object, skill or location ID.
    m_structureKeys[std::make_pair(Region::Cell::StructureQuarter, int(Region::Cell::DistrictTrade))] =
        L"Trade Quarter";
    m_structureKeys[std::make_pair(Region::Cell::StructureQuarter, int(Region::Cell::DistrictCrafting))] =
        L"Crafting Quarter";
    m_structureKeys[std::make_pair(Region::Cell::StructureQuarter, int(Region::Cell::DistrictClass))] =
        L"Class Quarter";
    m_structureKeys[std::make_pair(Region::Cell::StructureQuarter, int(Region::Cell::DistrictPet))] = L"Pet Quarter";
    m_structureKeys[std::make_pair(Region::Cell::StructurePortal, 0)] = L"Portal";
    m_structureKeys[std::make_pair(Region::Cell::StructurePalace, 0)] = L"Palace";

    for (std::map<int, std::wstring>::iterator it = m_creatureKeys.begin(); it != m_creatureKeys.end(); it++) {
        std::pair<const int, std::wstring> &entry = *it;
        m_creatureIds[entry.second] = entry.first;
    }
    for (std::map<int, std::wstring>::iterator it = m_staticKeys.begin(); it != m_staticKeys.end(); it++) {
        std::pair<const int, std::wstring> &entry = *it;
        m_staticIds[entry.second] = entry.first;
    }
    for (std::map<std::pair<int, int>, std::wstring>::iterator it = m_itemKeys.begin(); it != m_itemKeys.end(); it++) {
        std::pair<const std::pair<int, int>, std::wstring> &entry = *it;
        m_itemIds[entry.second] = entry.first;
    }

    for (int x = 0; x < 0x400; x++) {
        for (int y = 0; y < 0x400; y++) {
            m_regions[x][y] = 0;
            m_regionRecords[x][y] = 0;
        }
    }
    InitializeCriticalSectionAndSpinCount(&m_regionLock, 0x80000400);
    InitializeCriticalSectionAndSpinCount(&m_zoneLock, 0x80000400);

    m_buildingSprites[BuildingWoodFloor] = new Sprite(device, false);
    m_buildingSprites[BuildingWoodFloor]->load("framework-floor-wood.cub", 0, true);
    m_buildingSprites[BuildingWindowWall] = new Sprite(device, false);
    m_buildingSprites[BuildingWindowWall]->load("framework-wall-window.cub", 0, true);
    m_buildingSprites[BuildingDoorWall] = new Sprite(device, false);
    m_buildingSprites[BuildingDoorWall]->load("framework-wall-door.cub", 0, true);
    m_buildingSprites[BuildingRoof1] = new Sprite(device, false);
    m_buildingSprites[BuildingRoof1]->load("framework-roof1.cub", 0, true);
    m_buildingSprites[BuildingEntrance] = new Sprite(device, false);
    m_buildingSprites[BuildingEntrance]->load("framework-entrance.cub", 0, true);
    m_buildingSprites[BuildingWall] = new Sprite(device, false);
    m_buildingSprites[BuildingWall]->load("framework-wall.cub", 0, true);
    m_buildingSprites[BuildingRoof2] = new Sprite(device, false);
    m_buildingSprites[BuildingRoof2]->load("framework-roof2.cub", 0, true);
    m_buildingSprites[BuildingRoof3] = new Sprite(device, false);
    m_buildingSprites[BuildingRoof3]->load("framework-roof3.cub", 0, true);
    m_buildingSprites[BuildingBalconyWall] = new Sprite(device, false);
    m_buildingSprites[BuildingBalconyWall]->load("framework-wall-balcony.cub", 0, true);
    m_buildingSprites[BuildingRoofWall] = new Sprite(device, false);
    m_buildingSprites[BuildingRoofWall]->load("framework-roof-wall2.cub", 0, true);
    m_buildingSprites[BuildingStoneWell] = new Sprite(device, false);
    m_buildingSprites[BuildingStoneWell]->load("building-stone-well.cub", 0, true);
    m_buildingSprites[BuildingStoneStairs] = new Sprite(device, false);
    m_buildingSprites[BuildingStoneStairs]->load("building-stone-stairs2.cub", 0, true);

    m_spawnPoint = plasma::Vector<3, float>(8396928.0f, 8396928.0f, 100.0f);
    m_spawnPoint[2] = terrainHeight((int)m_spawnPoint[0], (int)m_spawnPoint[1], 0);
    unused8000fc = plasma::Vector<2, int>(0x8000, 0x8000);
    m_localPlayer = 0;
    m_petFoodRaces.push_back(RaceBunny);
    m_petFoodRaces.push_back(RaceMole);
    m_petFoodRaces.push_back(RaceFly);
    m_petFoodRaces.push_back(RaceCrow);
    m_petFoodRaces.push_back(RaceSheep);
    m_petFoodRaces.push_back(RaceBrownAlpaca);
    m_petFoodRaces.push_back(RaceAlpaca);
    m_petFoodRaces.push_back(RaceCat);
    m_petFoodRaces.push_back(RacePig);
    m_petFoodRaces.push_back(RaceHorse);
    m_petFoodRaces.push_back(RaceTurtle);
    m_petFoodRaces.push_back(RaceHornet);
    m_petFoodRaces.push_back(RacePeacock);
    m_petFoodRaces.push_back(RaceBarkBeetle);
    m_petFoodRaces.push_back(RaceSnoutBeetle);
    m_petFoodRaces.push_back(RaceLemonBeetle);
    m_petFoodRaces.push_back(RaceCollie);
    m_petFoodRaces.push_back(RaceBlueSlime);
    m_petFoodRaces.push_back(RaceGreenSlime);
    m_petFoodRaces.push_back(RacePinkSlime);
    m_petFoodRaces.push_back(RaceYellowSlime);
    m_petFoodRaces.push_back(RaceOwl);
    m_petFoodRaces.push_back(RacePenguin);
    m_petFoodRaces.push_back(RaceChicken);
    m_petFoodRaces.push_back(RaceSquirrel);
    m_petFoodRaces.push_back(RaceRaccoon);
    m_petFoodRaces.push_back(RaceMonkey);
    m_petFoodRaces.push_back(RaceScottishTerrier);
    m_petFoodRaces.push_back(RaceCrocodile);
    m_petFoodRaces.push_back(RaceTerrier);
    m_petFoodRaces.push_back(RaceSpitter);
    m_petFoodRaces.push_back(RaceCamel);
    m_petFoodRaces.push_back(RaceCrab);
    m_petFoodRaces.push_back(RacePlainRunner);
    m_petFoodRaces.push_back(RaceDesertRunner);
    m_petFoodRaces.push_back(RaceLeafRunner);
    m_petFoodRaces.push_back(RaceSnowRunner);
    m_petFoodRaces.push_back(RaceDuckbill);
    m_petFoodRaces.push_back(RacePorcupine);
    m_petFoodRaces.push_back(RaceSeagull);
    m_petFoodRaces.push_back(RaceParrot);
    m_petFoodRaces.push_back(RaceBat);
    m_petFoodRaces.push_back(RaceMidge);
    m_petFoodRaces.push_back(RaceMosquito);
    m_petFoodRaces.push_back(RaceBiter);
}

// Saves every zone of each loaded region and the region itself (unless the world saves nothing),
// deletes the regions, their records, the creatures and the values of m_airships.
World::~World()
{
    for (int x = 0; x < 0x400; x++) {
        for (int y = 0; y < 0x400; y++) {
            if (m_regions[x][y]) {
                if (!m_client) {
                    for (int i = 0; i < 64; i++) {
                        for (int j = 0; j < 64; j++)
                            saveZone(m_regions[x][y]->m_zones[i][j]);
                    }
                    saveRegion(x, y);
                }
                delete m_regions[x][y];
            }
            if (m_regionRecords[x][y])
                delete m_regionRecords[x][y];
        }
    }
    for (std::map<long long, Creature *>::iterator it = m_creatures.begin(); it != m_creatures.end(); ++it)
        delete it->second;
    for (std::map<long long, Zone::Airship *>::iterator it = m_airships.begin(); it != m_airships.end(); ++it)
        delete it->second;
    DeleteCriticalSection(&m_regionLock);
    DeleteCriticalSection(&m_zoneLock);
}

// The dictionary key of an ability, or L"" (the release keeps these in Speech).
std::wstring World::abilityKey(int id)
{
    std::map<int, std::wstring>::iterator it = m_abilityKeys.find(id);
    if (it == m_abilityKeys.end())
        return L"";
    return it->second;
}

// The same for a creature type (the seed and variant are not read).
void World::addCreature(const std::wstring &name, int race, int seed, int variant, Speech::Context &context)
{
    const Speech::Name &entry = m_speech.getName(creatureKey(race));
    for (std::list<std::wstring>::const_iterator tag = entry.tags.begin(); tag != entry.tags.end(); ++tag)
        context.tags.insert(std::wstring(L"#") + name + L"_" + *tag);
    for (std::map<std::wstring, std::wstring>::const_iterator form = entry.forms.begin(); form != entry.forms.end();
         ++form)
        context.variables[std::wstring(L"@") + name + L"_" + form->first] = form->second;
}

// Defines the item's dictionary tags (#name_tag) and forms (@name_form) for a dialog under the
// given name.
void World::addItem(const std::wstring &name, const Item &item, Speech::Context &context)
{
    const Speech::Name &entry = m_speech.getName(itemKey(item.category, item.id));
    for (std::list<std::wstring>::const_iterator tag = entry.tags.begin(); tag != entry.tags.end(); ++tag)
        context.tags.insert(std::wstring(L"#") + name + L"_" + *tag);
    for (std::map<std::wstring, std::wstring>::const_iterator form = entry.forms.begin(); form != entry.forms.end();
         ++form)
        context.variables[std::wstring(L"@") + name + L"_" + form->first] = form->second;
}

// The same for a region record's settlement, with an "@" in a form replaced by the settlement's
// generated name.
void World::addScenery(const std::wstring &name, Region::Record *scenery, Speech::Context &context)
{
    const Speech::Name &entry = m_speech.getName(settlementKey(scenery->kind, scenery->style));
    for (std::list<std::wstring>::const_iterator tag = entry.tags.begin(); tag != entry.tags.end(); ++tag)
        context.tags.insert(std::wstring(L"#") + name + L"_" + *tag);
    for (std::map<std::wstring, std::wstring>::const_iterator form = entry.forms.begin(); form != entry.forms.end();
         ++form) {
        std::wstring text = form->second;
        size_t at = text.find(L'@');
        if (at != std::wstring::npos)
            text.replace(at, 1, randomName(scenery->seed, -1));
        context.variables[std::wstring(L"@") + name + L"_" + form->first] = text;
    }
}

// Applies the buff locally and queues it with the creature as sender and recipient.
void World::applyBuff(Creature *creature, const Creature::Buff &buff, ServerUpdates *updates)
{
    creature->setBuff(buff);
    BuffPacket packet;
    packet.sourceId = creature->m_id;
    packet.targetId = creature->m_id;
    packet.buff = buff;
    updates->buffs.push_back(packet);
}

// A class skill with its own buff (GameController::update passes the skills 97 and 100..103):
// unless the skill is cooling down, the buff is applied, queued for the clients and the cooldown
// restarts.
namespace {
const int BulwarkDurationMs = 10000;
const int ManaShieldDurationMs = 30000;
}

void World::applyBuffSkill(Creature *creature, int ability, ServerUpdates *updates)
{
    if (creature->m_skillCooldowns[ability] > 0)
        return;
    Creature::Buff buff;
    BuffPacket packet;
    switch (ability) {
    case AbilityBulwark:
        buff.type = BuffBulwark;
        buff.timeRemaining = BulwarkDurationMs;
        buff.strength = 0.75f - creature->abilitySkillBonus(AbilityBulwark, -1) * 0.3f;
        creature->setBuff(buff);
        if (creature->m_entityData.stunTime > 0)
            creature->m_entityData.stunTime = 0;
        break;
    case AbilityManaShield:
        buff.type = BuffManashield;
        buff.timeRemaining = ManaShieldDurationMs;
        buff.strength = creature->spellPower() * 2.0f * (creature->abilitySkillBonus(AbilityManaShield, -1) + 1.0f);
        creature->setBuff(buff);
        break;
    case AbilitySwiftness:
        buff.type = BuffTempoBoost;
        buff.timeRemaining = 10000;
        buff.strength = creature->abilitySkillBonus(AbilitySwiftness, -1) + 1.0f;
        creature->setBuff(buff);
        break;
    case AbilityCamouflage:
        buff.type = BuffCamouflage;
        buff.timeRemaining = (int)(creature->abilitySkillBonus(AbilityCamouflage, -1) * 12000.0f + 8000.0f);
        creature->setBuff(buff);
        break;
    case AbilityWarFrenzy:
        buff.type = BuffWarFrenzy;
        buff.timeRemaining = 10000;
        creature->setBuff(buff);
        break;
    default:
        creature->m_skillCooldowns[ability] = creature->cooldown(ability);
        return;
    }
    packet.buff = buff;
    packet.sourceId = creature->m_id;
    packet.targetId = creature->m_id;
    updates->buffs.push_back(packet);
    creature->m_skillCooldowns[ability] = creature->cooldown(ability);
}

namespace {
const int WakeupStunTimeMs = 3000;
}

// A hit from a client or from the world's own fights (applyHit). The server passes a hit on a
// player to that player's client. Otherwise: the target's stealth falls and a sleeper wakes,
// threat for the attacker in the target's table (m_aggro), the push, the action lock, then the
// hit's type: 1 gives the target mana, 3 does nothing, 4 is a dodge and 5 is taken by the target's
// shield buffs; the damage, or the healing when it is negative. When the target dies: its sound,
// the spawn's object disabled, its loot, its companion's death, the mission of its eight-zone
// square advanced (a completed one removes the square's extra spawns and rewards each player who
// fought), the kill packets and xp, and its spawn's respawn timer.
void World::applyHit(const HitPacket &hit, ServerUpdates *updates, std::set<Zone *> *changedZones)
{
    Creature *target = creatureByID(hit.targetId);
    if (!target || target->m_entityData.hp <= 0.0f)
        return;
    if (!m_localPlayer && target->m_entityData.hostilityType == HostilityPlayer) {
        updates->hits.push_back(hit);
        return;
    }
    Creature *attacker = creatureByID(hit.attackerId);
    if (hit.damage > 0.0f) {
        target->m_stealth -= hit.damage * 0.25f /
                             (float)pow(2.0, (target->m_entityData.level - 1 + target->m_entityData.rarity) * 0.25f);
        if (target->m_stealth < 0.0f)
            target->m_stealth = 0.0f;
        if (target->m_entityData.currentAbility == AbilitySleep) {
            target->m_entityData.currentAbility = AbilityNone;
            target->m_entityData.stunTime = WakeupStunTimeMs;
        }
    }
    if (hit.damage >= 0.0f && attacker) {
        attacker->m_opponentId = hit.targetId;
        target->m_opponentId = hit.attackerId;
        attacker->m_hitCounts[hit.targetId] += 1.0f;
        if (attacker->isAttackingFromRange())
            target->m_aggro[hit.attackerId] += hit.damage + 0.01f;
        else if (attacker->m_entityData.race == RaceTurtle)
            target->m_aggro[hit.attackerId] += hit.damage * 10.0f + 0.01f;
        else if (attacker->m_entityData.equipment.weaponRight.category == ItemCategoryWeapon &&
                 attacker->m_entityData.equipment.weaponRight.isTwoHanded() &&
                 attacker->m_entityData.specialization == SpecializationGuardian)
            target->m_aggro[hit.attackerId] += hit.damage * 20.0f + 0.01f;
        else
            target->m_aggro[hit.attackerId] += hit.damage * 2.0f + 0.01f;
        onAttack(attacker, target, updates);
    }
    if (target->m_entityData.appearance.appearanceFlags & (1 << AppearanceFlagMissionResident)) {
        // Such a creature takes hits only once a player is among its attackers.
        bool attackedByPlayer = false;
        for (std::map<long long, float>::iterator it = target->m_aggro.begin(); it != target->m_aggro.end(); ++it) {
            if (it->second > 0.0f) {
                Creature *creature = creatureByID(it->first);
                if (creature && creature->m_entityData.hostilityType == HostilityPlayer) {
                    attackedByPlayer = true;
                    break;
                }
            }
        }
        if (!attackedByPlayer)
            return;
    }
    if (hit.hitType == HitTypeMiss)
        return;
    if (hit.damage >= 0.0f) {
        plasma::Vector<3, float> push = hit.direction;
        if (target->m_entityData.stunTime > 0 && hit.stunTime <= 0) {
            push[0] *= 0.1f;
            push[1] *= 0.1f;
            push[2] *= 0.1f;
        }
        if (target->m_entityData.hostilityType != HostilityAttackableObject &&
            ((!m_client && target->m_entityData.hostilityType != HostilityPlayer) || target == m_localPlayer) &&
            push[0] * push[0] + push[1] * push[1] + push[2] * push[2] >=
                target->m_entityData.retreat[0] * target->m_entityData.retreat[0] +
                    target->m_entityData.retreat[1] * target->m_entityData.retreat[1] +
                    target->m_entityData.retreat[2] * target->m_entityData.retreat[2])
            target->m_entityData.retreat = push;
        if (hit.stunTime > 0)
            target->m_entityData.velocity = plasma::Vector<3, float>(0, 0, 0);
    }
    if (hit.actionLockTime > 0)
        target->m_entityData.actionLockTime = std::max(target->m_entityData.actionLockTime, hit.actionLockTime);
    if (hit.hitType == HitTypeBlock) {
        target->m_entityData.mp += 0.25f;
        if (target->m_entityData.mp > 1.0f)
            target->m_entityData.mp = 1.0f;
        return;
    }
    if (hit.hitType == HitTypeDodge) {
        target->applyDodgeBonus(updates);
        return;
    }
    if (hit.stunTime > 0)
        target->m_entityData.stunTime = std::max(target->m_entityData.stunTime, hit.stunTime);
    if (m_client && target != m_localPlayer)
        return;
    if (hit.damage > 0.0f) {
        if (hit.hitType == HitTypeMana) {
            // The hit is taken by the target's buffs of type 6 (a shield).
            for (std::list<Creature::Buff>::iterator buff = target->m_buffs.begin(); buff != target->m_buffs.end();
                 ++buff) {
                if (buff->type == BuffManashield) {
                    buff->strength -= hit.damage;
                    if (buff->strength <= 0.0f) {
                        buff->strength = 0.0f;
                        buff->timeRemaining = 0;
                    }
                    BuffPacket packet;
                    packet.sourceId = hit.attackerId;
                    packet.targetId = hit.targetId;
                    packet.buff = *buff;
                    updates->buffs.push_back(packet);
                }
            }
        } else if (target->canBeDamaged() && (!attacker || attacker->canBeDamaged())) {
            target->m_entityData.hp -= hit.damage;
        }
    }
    if (hit.damage < 0.0f) {
        // Healing; the creatures that fight the target turn on the healer.
        target->m_entityData.hp -= hit.damage;
        if (target->m_entityData.hp > target->maxHP())
            target->m_entityData.hp = target->maxHP();
        if (!m_client) {
            for (std::map<long long, Creature *>::iterator it = m_creatures.begin(); it != m_creatures.end(); ++it) {
                // Attacker is not tested (areEnemies reads it when the healer is gone)
                if (it->second && areEnemies(attacker, it->second) &&
                    it->second->m_aggro.find(target->m_id) != it->second->m_aggro.end()) {
                    it->second->m_aggro[attacker->m_id] -= hit.damage * 0.1f;
                    onAttack(attacker, it->second, updates);
                }
            }
        }
    }
    if (target->m_entityData.hp <= 0.0f) {
        target->m_entityData.hp = 0.0f;
        ServerUpdates::SoundPacket sound;
        sound.position = target->m_entityData.position;
        sound.sound = SoundDestroy2;
        updates->sounds.push_back(sound);
        target->m_entityData.timeSinceAbility = 0;

        // The spawn's home object (a type-5 object of its zone that names the spawn) becomes inactive.
        plasma::Vector<3, int> &home = target->m_entityData.spawnSlot;
        Zone *zone = this->zone(home[0], home[1]);
        if (zone) {
            int index = 0;
            std::vector<Zone::Object>::iterator object = zone->m_objects.begin();
            while (object != zone->m_objects.end()) {
                if (object->type == StaticObjectGate && object->linkedObjectIndex == home[2] && object->active) {
                    setObjectActive(&*object, updates, false);
                    updates->objects.push_back(
                        ServerUpdates::ZoneObjectPacket(plasma::Vector<3, int>(home[0], home[1], index), *object));
                    break;
                }
                ++object;
                ++index;
            }
        }
        dropLoot(target, changedZones);
        for (std::map<long long, Creature *>::iterator it = m_creatures.begin(); it != m_creatures.end(); ++it) {
            if (it->second && it->second->m_entityData.hostilityType == HostilityPlayer) {
                // Computed and not used.
                plasma::Vector<3, Fixed> offset = it->second->m_entityData.position - target->m_entityData.position;
            }
        }
        if (target->m_companionId != 0 && creatureByID(target->m_companionId))
            creatureByID(target->m_companionId)->m_entityData.hp = 0.0f;

        float xp = levelBonus((float)(target->m_entityData.level + target->m_entityData.rarity)) * 20.0f;
        if (xp < 1.0f)
            xp = 1.0f;
        if (target->isBoss())
            xp *= 20.0f;
        unsigned short appearanceFlags = target->m_entityData.appearance.appearanceFlags;
        if (appearanceFlags & (1 << AppearanceFlagTinted))
            xp *= 10.0f;
        if (appearanceFlags & (1 << AppearanceFlagSummoned))
            xp = 0.0f;

        bool missionCompleted = false;
        MissionState mission;
        if (!m_client) {
            Region::Record *scenery = getRecord(home[0] / 8, home[1] / 8);
            if (scenery && scenery->mission.progress != MissionCompleted &&
                ((appearanceFlags & (1 << AppearanceFlagMissionResident)) ||
                 (scenery->mission.kind == MissionDungeon && target->m_dungeonMissionTarget))) {
                bool killedByPlayer = false;
                for (std::map<long long, float>::iterator it = target->m_aggro.begin(); it != target->m_aggro.end();
                     ++it) {
                    if (it->second > 0.0f && creatureByID(it->first) &&
                        creatureByID(it->first)->m_entityData.hostilityType == HostilityPlayer) {
                        killedByPlayer = true;
                        break;
                    }
                }
                if (killedByPlayer) {
                    bool advanced = true;
                    switch (scenery->mission.kind) {
                    case MissionInvasion:
                    case MissionSceneryInvasion:
                    case MissionVillageInvasion:
                    case MissionCaveInvasion:
                    case MissionRiverInvasion:
                    case MissionGang:
                        scenery->mission.killCount++;
                        scenery->mission.progress = MissionInProgress;
                        if (scenery->mission.killCount < scenery->mission.targetKillCount)
                            break;
                        // Fall through: the last kill completes the mission
                    case MissionMonster:
                    case MissionRareBoss:
                    case MissionVillageMonster:
                    case MissionVillageRareBoss:
                    case MissionDungeon:
                    case MissionGangBoss:
                        missionCompleted = true;
                        scenery->mission.progress = MissionCompleted;
                        break;
                    default:
                        advanced = false;
                        break;
                    }
                    if (advanced) {
                        mission = scenery->mission;
                        ServerUpdates::MissionPacket packet;
                        packet.position = plasma::Vector<2, int>(home[0] / 8, home[1] / 8);
                        packet.mission = mission;
                        updates->missions.push_back(packet);
                        if (missionCompleted) {
                            // The square's zones lose the spawns added after generation.
                            for (int x = home[0] / 8 * 8; x < home[0] / 8 * 8 + 8; x++) {
                                for (int y = home[1] / 8 * 8; y < home[1] / 8 * 8 + 8; y++) {
                                    Zone *squareZone = this->zone(x, y);
                                    if (!squareZone)
                                        continue;
                                    for (int i = squareZone->m_baseSpawnCount; i < (int)squareZone->m_spawns.size();
                                         i++)
                                        delete squareZone->m_spawns[i];
                                    squareZone->m_spawns.resize(squareZone->m_baseSpawnCount);
                                }
                            }
                        }
                    }
                }
            }
        }

        // Every creature forgets the dead one and its companion; each player the target fought gets the
        // kill (and the mission's rewards).
        for (std::map<long long, Creature *>::iterator it = m_creatures.begin(); it != m_creatures.end(); ++it) {
            if (!it->second)
                continue;
            if (it->second->m_opponentId == hit.targetId || it->second->m_opponentId == target->m_companionId)
                it->second->m_opponentId = 0;
            std::list<std::map<long long, float>::iterator> stale;
            for (std::map<long long, float>::iterator entry = it->second->m_aggro.begin();
                 entry != it->second->m_aggro.end(); ++entry) {
                if (entry->first == hit.targetId || entry->first == target->m_companionId)
                    stale.push_back(entry);
            }
            for (std::list<std::map<long long, float>::iterator>::iterator entry = stale.begin(); entry != stale.end();
                 ++entry)
                it->second->m_aggro.erase(*entry);
            if (target->m_entityData.hostilityType == HostilityEnemy &&
                it->second->m_entityData.hostilityType == HostilityPlayer &&
                target->m_aggro.find(it->first) != target->m_aggro.end()) {
                KillPacket kill;
                kill.targetId = hit.targetId;
                kill.targetRace = target->m_entityData.race;
                kill.killerId = it->first;
                kill.xp = (int)xp;
                updates->kills.push_back(kill);
                onKill(kill);
                if (missionCompleted) {
                    // A category-13 item whose level is the amount, then a weapon or an armour piece of the mission's
                    // level.
                    int rarity = mission.rarity;
                    int amount = (int)(levelBonus((float)(mission.level + rarity)) * 50.0f + 1.0f);
                    ServerUpdates::ItemPickupPacket reward;
                    reward.creatureId = it->first;
                    reward.item.level = amount;
                    reward.item.category = ItemCategoryPlatinumCoin;
                    updates->pickedUpItems.push_back(reward);
                    if (it->second == m_localPlayer)
                        it->second->m_inventory.add(reward.item);
                    if (rand() % 2)
                        reward.item = randomWeapon(mission.level, rarity, it->second->m_entityData.classType);
                    else
                        reward.item = randomArmor(mission.level, rarity, it->second->m_entityData.classType);
                    updates->pickedUpItems.push_back(reward);
                    if (it->second == m_localPlayer)
                        it->second->m_inventory.add(reward.item);
                }
            }
        }

        // The spawn comes back in twenty minutes.
        Zone *homeZone = this->zone(home[0], home[1]);
        if (homeZone && home[2] >= 0 && home[2] < (int)homeZone->m_spawns.size()) {
            homeZone->m_spawns[home[2]]->respawnTimeRemaining = 1200000;
            homeZone->m_spawns[home[2]]->deathDay = m_day;
        }
    }
}

// Whether the creature may attack the other: a pet (hostility type 5) never attacks a player (0)
// and creatures of type 6 are left alone; a hostile creature (1) and another are enemies unless
// both are hostile and equally peaceful; otherwise only creatures with the combat side override
// are. areEnemies (the same tests of hostility types and toggles)
bool World::areEnemies(Creature *creature, Creature *other)
{
    if (creature->m_entityData.hostilityType == HostilityPet && other->m_entityData.hostilityType == HostilityPlayer)
        return false;
    if (creature->m_entityData.hostilityType == HostilityAttackableObject ||
        other->m_entityData.hostilityType == HostilityAttackableObject)
        return false;
    if (creature->m_entityData.hostilityType == HostilityEnemy &&
        (other->m_entityData.hostilityType != creature->m_entityData.hostilityType ||
         other->isPeaceful() != creature->isPeaceful()))
        return true;
    if (other->m_entityData.hostilityType == HostilityEnemy &&
        (creature->m_entityData.hostilityType != HostilityEnemy || creature->isPeaceful() != other->isPeaceful()))
        return true;
    if (creature->m_entityData.binaryToggles.combatSideOverride || other->m_entityData.binaryToggles.combatSideOverride)
        return true;
    return false;
}

namespace {
const int AttackParticleCount = 8;
const int IceSpiritSlowCapMs = 5000;
const int WindSpiritSprintCapMs = 5000;
const int FireStunBuffDurationMs = 3000;
const int StunRecoveryCooldownMs = 3000;
}

// One creature's attack on another (attackCreature). A dead target, the local player on a client,
// or an attacker's ground-only ability against a creature in the air void the attack. An enemy
// that dodges makes it a miss; a blocking target spends blockPower and gains mana; manashield
// buffs absorb damage. Then armour or resistance, less with the attacker's combo, the weapons'
// spirits, the hit flash and the hit's sound, the push, bulwark buffs, an ambush's poison, a groan
// on a critical hit and the stun chance. Every outcome is queued as a hit and, on the server,
// applied at once.
bool World::attackCreature(Creature *target, Creature *attacker, float damage, bool critical, bool powered,
                           float stunChance, const plasma::Vector<3, Fixed> &position,
                           const plasma::Vector<3, float> &direction, ServerUpdates *updates,
                           std::set<Zone *> *changedZones, bool magical, int ability, BulletPacket *projectile,
                           bool allowBlocking)
{
    if (target->m_entityData.hp <= 0.0f)
        return false;
    if (m_client && target == m_localPlayer)
        return false;
    if (attacker) {
        if ((attacker->m_entityData.currentAbility == AbilityQuake ||
             attacker->m_entityData.currentAbility == AbilityStomp) &&
            !(target->m_entityData.flags & (1 << CreatureFlagOnGround)))
            return false;
        if (attacker->m_entityData.currentAbility == AbilitySupportRay) {
            ServerUpdates::ParticlePacket particles;
            particles.position = target->m_entityData.position;
            particles.size = (float)rand() * 0.1f / float(RAND_MAX) + 0.1f;
            particles.type = ParticleFire;
            particles.acceleration = plasma::Vector<3, float>(0.0f, 0.0f, 10.0f);
            // Attack-effect RGBA palette.
            particles.count = AttackParticleCount;
            particles.color = plasma::Vector<4, float>(0.6f, 0.6f, 1.0f, 1.0f);
            updates->particles.push_back(particles);
        }
        if (target->m_entityData.hostilityType == HostilityEnemy &&
            (target->m_entityData.binaryToggles.dodge ||
             (target->m_entityData.flags & (1 << CreatureFlagMovementBlocked)))) {
            ServerUpdates::SoundPacket sound;
            sound.position = target->m_entityData.position;
            sound.pitch = (float)rand() * 0.3f / float(RAND_MAX) + 1.0f;
            sound.sound = SoundBlock;
            updates->sounds.push_back(sound);
            HitPacket hit;
            hit.position = target->m_entityData.position;
            hit.attackerId = attacker->m_id;
            hit.targetId = target->m_id;
            hit.damage = damage;
            hit.hitType = HitTypeMiss;
            hit.critical = 0;
            hit.direction = direction;
            hit.stunTime = 0;
            updates->hits.push_back(hit);
            if (!m_client)
                applyHit(hit, updates, changedZones);
            return false;
        }
        if (allowBlocking && target->isBlocking() && target->m_entityData.blockPower > 0.0f) {
            float cost = damage / target->blockPower();
            target->m_entityData.mp += target->m_entityData.blockPower;
            target->m_entityData.charge += target->m_entityData.blockPower;
            if (target->m_entityData.mp > 1.0f)
                target->m_entityData.mp = 1.0f;
            if (target->m_entityData.charge > target->m_entityData.mp)
                target->m_entityData.charge = target->m_entityData.mp;
            HitPacket hit;
            hit.position = target->m_entityData.position;
            hit.attackerId = attacker->m_id;
            hit.targetId = target->m_id;
            hit.damage = damage;
            hit.hitType = HitTypeBlock;
            hit.critical = 0;
            hit.stunTime = 0;
            hit.direction = direction * 0.5f;
            target->m_entityData.blockPower -= cost;
            if (target->m_entityData.blockPower < -1.0f)
                target->m_entityData.blockPower = -1.0f;
            updates->hits.push_back(hit);
            ServerUpdates::SoundPacket sound;
            sound.position = target->m_entityData.position;
            sound.pitch = (float)rand() * 0.3f / float(RAND_MAX) + 1.0f;
            sound.sound = SoundBlock;
            updates->sounds.push_back(sound);
            if (!m_client)
                applyHit(hit, updates, changedZones);
            return false;
        }
        float absorption = 0.0f;
        for (std::list<Creature::Buff>::iterator buff = target->m_buffs.begin(); buff != target->m_buffs.end();
             ++buff) {
            if (buff->type == BuffManashield)
                absorption += buff->strength;
        }
        // The attacker's combo pierces armour and manashields.
        float armorPiercing = std::min(1.0f, (float)attacker->m_entityData.hitCombo / (float)attacker->maxCombo());
        absorption *= 1.0f - armorPiercing;
        if (absorption > 0.0f) {
            HitPacket hit;
            hit.position = target->m_entityData.position;
            hit.attackerId = attacker->m_id;
            hit.targetId = target->m_id;
            hit.damage = damage;
            hit.hitType = HitTypeMana;
            hit.critical = 0;
            hit.stunTime = 0;
            hit.direction = direction * 0.5f;
            updates->hits.push_back(hit);
            ServerUpdates::SoundPacket sound;
            sound.position = target->m_entityData.position;
            sound.pitch = (float)rand() * 0.3f / float(RAND_MAX) + 1.0f;
            sound.sound = SoundAbsorb;
            updates->sounds.push_back(sound);
            if (!m_client)
                applyHit(hit, updates, changedZones);
            damage -= absorption;
            if (damage <= 0.0f)
                return true;
        }
        float protection = magical ? target->resistance() : target->armor();
        damage = std::max(0.0f, damage - protection * (1.0f - armorPiercing));

        float combo = (float)attacker->m_entityData.hitCombo / (float)attacker->maxCombo();
        if (combo > 0.25f) {
            // Spirit particles fly off along the hit, upwards.
            plasma::Vector<3, float> impulse = direction;
            if (impulse.squaredLength() > 0.0f)
                impulse.normalize();
            impulse[0] *= 4.0f;
            impulse[1] *= 4.0f;
            impulse[2] = 5.0f;
            std::list<Item *> weapons = attacker->attackWeapons();
            for (std::list<Item *>::iterator weapon = weapons.begin(); weapon != weapons.end(); ++weapon)
                createSpiritParticles(target->m_entityData.position, impulse, combo, powered, **weapon, updates);
        }
        if (powered && combo > 0.0f) {
            // Each spirit of the weapons adds damage by its level: fire fully, unholy a little and heals the
            // attacker, ice slows the target and wind speeds the attacker up.
            std::list<Item *> weapons = attacker->attackWeapons();
            if (!weapons.empty()) {
                float healing = 0.0f;
                float strength = 0.1f / (float)weapons.size();
                for (std::list<Item *>::iterator weapon = weapons.begin(); weapon != weapons.end(); ++weapon) {
                    for (int i = 0; i < (*weapon)->spiritCount; i++) {
                        switch ((*weapon)->spirits[i].material) {
                        case ItemMaterialUnholySpirit: {
                            float bonus = levelBonus((float)(*weapon)->spirits[i].level) * strength * combo;
                            healing += bonus * 2.0f;
                            damage += bonus * 0.5f;
                            break;
                        }
                        case ItemMaterialFireSpirit:
                            damage += levelBonus((float)(*weapon)->spirits[i].level) * strength * combo;
                            break;
                        case ItemMaterialIceSpirit:
                            damage += levelBonus((float)(*weapon)->spirits[i].level) * (strength * 0.25f) * combo;
                            target->m_entityData.slowedTime += combo * 500.0f;
                            break;
                        case ItemMaterialWindSpirit:
                            damage += levelBonus((float)(*weapon)->spirits[i].level) * (strength * 0.25f) * combo;
                            attacker->m_entityData.sprintTime += combo * 500.0f;
                            break;
                        }
                    }
                }
                if (target->m_entityData.slowedTime > IceSpiritSlowCapMs)
                    target->m_entityData.slowedTime = IceSpiritSlowCapMs;
                if (attacker->m_entityData.sprintTime > WindSpiritSprintCapMs)
                    attacker->m_entityData.sprintTime = WindSpiritSprintCapMs;
                if (healing > 0.0f) {
                    HitPacket hit;
                    hit.position = attacker->m_entityData.position;
                    hit.attackerId = attacker->m_id;
                    hit.targetId = attacker->m_id;
                    hit.damage = -healing;
                    updates->hits.push_back(hit);
                    attacker->m_entityData.hp += healing;
                }
            }
        }
        if (attacker->m_entityData.currentAbility == AbilitySupportRay) {
            if (target->m_hitFlash < 0.3f)
                target->m_hitFlash = 0.3f;
        } else if (allowBlocking) {
            target->m_hitFlash = 1.0f;
        }
        if (allowBlocking) {
            if (magical) {
                if (attacker->m_entityData.specialization == SpecializationWater) {
                    ServerUpdates::SoundPacket sound;
                    sound.position = target->m_entityData.position;
                    sound.pitch = 1.0f;
                    sound.sound = SoundWatersplashHit;
                    updates->sounds.push_back(sound);
                } else {
                    ServerUpdates::SoundPacket sound;
                    sound.position = target->m_entityData.position;
                    sound.pitch = 1.5f;
                    sound.sound = SoundFireHit;
                    updates->sounds.push_back(sound);
                }
            } else if (ability != AbilitySupportRay) {
                ServerUpdates::SoundPacket sound;
                sound.position = target->m_entityData.position;
                std::list<Item *> weapons = attacker->attackWeapons();
                int weaponType = -1;
                if (!weapons.empty())
                    weaponType = weapons.front()->id;
                if (attacker->m_entityData.currentAbility == AbilityHeadbutt)
                    weaponType = WeaponGreatmace;
                sound.pitch = (float)rand() * 0.2f / float(RAND_MAX) + 0.9f;
                int weaponSound;
                if (critical) {
                    switch (weaponType) {
                    case WeaponDagger:
                    case WeaponFist:
                        weaponSound = SoundPunch2;
                        break;
                    case WeaponSword:
                    case WeaponAxe:
                        sound.pitch += 0.1f;
                        // Fall through
                    case WeaponGreatsword:
                    case WeaponGreataxe:
                        weaponSound = SoundBlade2;
                        break;
                    case WeaponLongsword:
                        weaponSound = SoundLongBlade2;
                        break;
                    case WeaponMace:
                    case WeaponGreatmace:
                        weaponSound = SoundHit2;
                        break;
                    case WeaponBow:
                    case WeaponCrossbow:
                    case WeaponBoomerang:
                        weaponSound = SoundHitArrowCritical;
                        break;
                    default:
                        weaponSound = SoundHit2;
                        break;
                    }
                } else {
                    switch (weaponType) {
                    case WeaponDagger:
                    case WeaponFist:
                        weaponSound = SoundPunch1;
                        break;
                    case WeaponSword:
                    case WeaponAxe:
                        sound.pitch += 0.1f;
                        // Fall through
                    case WeaponGreatsword:
                    case WeaponGreataxe:
                        weaponSound = SoundBlade1;
                        break;
                    case WeaponLongsword:
                        weaponSound = SoundLongBlade1;
                        break;
                    case WeaponBow:
                    case WeaponCrossbow:
                    case WeaponBoomerang:
                        weaponSound = SoundHitArrow;
                        break;
                    case WeaponMace:
                    case WeaponGreatmace:
                        weaponSound = SoundHit1;
                        break;
                    default:
                        weaponSound = SoundHit1;
                        break;
                    }
                }
                // Called and not used.
                attacker->hasTwoHandedWeapon();
                if (attacker->m_entityData.currentAbility == AbilitySmash)
                    sound.sound = SoundSmash1;
                else
                    sound.sound = attacker->m_entityData.currentAbility == AbilityBlockJab ? SoundHit1 : weaponSound;
                updates->sounds.push_back(sound);
            }
        }
    } else {
        rand();
        target->m_hitFlash = 0.5f;
    }

    // The target moves no faster than the hit pushes it.
    float speedSquared = target->m_entityData.velocity.squaredLength();
    if (direction.squaredLength() > speedSquared)
        speedSquared = direction.squaredLength();
    if (target->m_entityData.velocity.squaredLength() > speedSquared) {
        plasma::Vector<3, float> velocity = target->m_entityData.velocity;
        velocity.normalize();
        velocity *= (float)sqrt(speedSquared);
        target->m_entityData.velocity = velocity;
    }

    HitPacket hit;
    hit.position = target->m_entityData.position;
    hit.attackerId = attacker ? attacker->m_id : 0;
    for (std::list<Creature::Buff>::iterator buff = target->m_buffs.begin(); buff != target->m_buffs.end(); ++buff) {
        if (buff->type == BuffBulwark)
            damage *= buff->strength;
    }
    hit.targetId = target->m_id;
    hit.damage = damage;
    hit.critical = critical;
    hit.stunTime = 0;
    hit.direction = direction;
    hit.powered = powered;
    if (ability == AbilityAmbush) {
        // An ambush poisons the target (attacker is not tested).
        Creature::Buff buff;
        buff.timeRemaining = FireStunBuffDurationMs;
        buff.type = BuffPoisoned;
        buff.sourceId = attacker->m_id;
        buff.strength = damage * 0.1f;
        target->m_buffs.push_back(buff);
        BuffPacket packet;
        packet.sourceId = attacker->m_id;
        packet.targetId = target->m_id;
        packet.buff = buff;
        updates->buffs.push_back(packet);
    }
    if (critical && target->m_entityData.hostilityType != HostilityAttackableObject) {
        ServerUpdates::SoundPacket sound;
        sound.position = target->m_entityData.position;
        sound.pitch = (float)rand() * 0.1f / float(RAND_MAX) + 1.0f;
        switch (target->m_entityData.race) {
        case RaceElfMale:
            sound.sound = SoundElfMaleGroan;
            updates->sounds.push_back(sound);
            break;
        case RaceHumanMale:
        case RaceWizard:
            sound.sound = SoundHumanMaleGroan;
            updates->sounds.push_back(sound);
            break;
        case RaceDwarfMale:
            sound.sound = SoundDwarfMaleGroan;
            updates->sounds.push_back(sound);
            break;
        case RaceGoblinMale:
            sound.sound = SoundGoblinMaleGroan;
            updates->sounds.push_back(sound);
            break;
        case RaceUndeadMale:
            sound.sound = SoundUndeadMaleGroan;
            updates->sounds.push_back(sound);
            break;
        case RaceLizardmanMale:
            sound.sound = SoundLizardMaleGroan;
            updates->sounds.push_back(sound);
            break;
        case RaceHumanFemale:
        case RaceWitch:
            sound.sound = SoundHumanFemaleGroan;
            updates->sounds.push_back(sound);
            break;
        case RaceElfFemale:
            sound.sound = SoundElfFemaleGroan;
            updates->sounds.push_back(sound);
            break;
        case RaceDwarfFemale:
            sound.sound = SoundDwarfFemaleGroan;
            updates->sounds.push_back(sound);
            break;
        case RaceGoblinFemale:
            sound.sound = SoundGoblinFemaleGroan;
            updates->sounds.push_back(sound);
            break;
        case RaceUndeadFemale:
            sound.sound = SoundUndeadFemaleGroan;
            updates->sounds.push_back(sound);
            break;
        case RaceLizardmanFemale:
            sound.sound = SoundLizardFemaleGroan;
            updates->sounds.push_back(sound);
            break;
        case RaceOrcMale:
            sound.sound = SoundOrcMaleGroan;
            updates->sounds.push_back(sound);
            break;
        case RaceOrcFemale:
            sound.sound = SoundOrcFemaleGroan;
            updates->sounds.push_back(sound);
            break;
        case RaceFrogmanMale:
            sound.sound = SoundFrogmanMaleGroan;
            updates->sounds.push_back(sound);
            break;
        case RaceFrogmanFemale:
            sound.sound = SoundFrogmanFemaleGroan;
            updates->sounds.push_back(sound);
            break;
        case RaceOgre:
        case RaceTroll:
        case RaceDarkTroll:
        case RaceYeti:
        case RaceCyclops:
            sound.sound = SoundTrollGroan;
            updates->sounds.push_back(sound);
            break;
        case RaceGnoll:
        case RaceGnobold:
        case RaceMole:
            sound.sound = SoundMoleGroan;
            updates->sounds.push_back(sound);
            break;
        case RaceGreenSlime:
        case RacePinkSlime:
        case RaceYellowSlime:
        case RaceBlueSlime:
            sound.sound = SoundSlimeGroan;
            updates->sounds.push_back(sound);
            break;
        case RaceZombie:
            sound.sound = SoundZombieGroan;
            updates->sounds.push_back(sound);
            break;
        case RaceSaurian:
            sound.sound = SoundMonsterGroan;
            updates->sounds.push_back(sound);
            break;
        }
    }
    if (attacker) {
        if (target->m_entityData.stunTime < -StunRecoveryCooldownMs &&
            target->m_entityData.hostilityType != HostilityAttackableObject) {
            if (attacker->m_entityData.currentAbility == AbilityTwoHandedThrust ||
                attacker->m_entityData.currentAbility == AbilityCrossedWeaponSweep ||
                attacker->m_entityData.currentAbility == AbilityHeadbutt ||
                attacker->m_entityData.currentAbility == Ability93 ||
                attacker->m_entityData.currentAbility == AbilityHornCharge)
                stunChance = 1.0f;
            if (target->isBoss())
                stunChance *= 0.15f;
            if (!attacker->isBoss() && attacker->m_entityData.hostilityType != HostilityPlayer)
                stunChance *= 0.1f;
            if (attacker->m_entityData.currentAbility == AbilityQuake ||
                attacker->m_entityData.currentAbility == AbilityStomp)
                stunChance = 1.0f;
            if (target->m_entityData.currentAbility == AbilitySleep)
                stunChance = 1.0f;
            // Doubled for every tenth of a level-bonus step the attacker is ahead.
            stunChance *= (float)pow(
                2.0, (levelBonus((float)attacker->m_entityData.level) - levelBonus((float)target->m_entityData.level)) *
                         10.0f);
            if (stunChance > 1.0f)
                stunChance = 1.0f;
            stunChance *= 0.9f;
            for (std::list<Creature::Buff>::iterator buff = target->m_buffs.begin(); buff != target->m_buffs.end();
                 ++buff) {
                if (buff->type == BuffBulwark)
                    stunChance = 0.0f;
            }
            if (stunChance > (float)rand() / float(RAND_MAX)) {
                hit.stunTime = target->maxStunTime();
                hit.direction[2] = 10.0f;
                ServerUpdates::SoundPacket sound;
                sound.position = target->m_entityData.position;
                sound.pitch = 1.0f;
                sound.sound = SoundSwordHit03;
                updates->sounds.push_back(sound);
            }
        }
        if (attacker->m_entityData.currentAbility == AbilitySupportRay)
            hit.supportRayLight = 1;
    }
    updates->hits.push_back(hit);
    if (!m_client)
        applyHit(hit, updates, changedZones);
    return true;
}

Sprite *World::buildingSprite(int modelIndex)
{
    std::map<int, Sprite *>::iterator it = m_buildingSprites.find(modelIndex);
    if (it == m_buildingSprites.end())
        return 0;
    return it->second;
}

// Whether two creatures are enemies: as areEnemies, except that a creature is no enemy of itself,
// creatures of type 6 are everyone's enemies and pets may attack players.
bool World::isHostile(Creature *creature, Creature *other)
{
    if (creature == other)
        return false;
    if (creature->m_entityData.hostilityType == HostilityAttackableObject ||
        other->m_entityData.hostilityType == HostilityAttackableObject)
        return true;
    if (creature->m_entityData.hostilityType == HostilityEnemy &&
        (other->m_entityData.hostilityType != creature->m_entityData.hostilityType ||
         other->isPeaceful() != creature->isPeaceful()))
        return true;
    if (other->m_entityData.hostilityType == HostilityEnemy &&
        (creature->m_entityData.hostilityType != HostilityEnemy || creature->isPeaceful() != other->isPeaceful()))
        return true;
    if (creature->m_entityData.binaryToggles.combatSideOverride || other->m_entityData.binaryToggles.combatSideOverride)
        return true;
    return false;
}

float World::recordInfluence(int x, int y)
{
    Region::Record *record = getRecord(x / BlocksPerSceneryRecord, y / BlocksPerSceneryRecord);
    if (!record)
        return 0.0f;
    if (record->kind != Region::Record::SceneryCity)
        return 0.0f;
    return record->influence(Fixed(x), Fixed(y));
}

// The display name of an item. Formulas are named after their product, pets after their creature;
// category 21 is "<generated name>'s <item>". Otherwise the material's "item" form with "@"
// replaced by the item's singular form, then, for a nonzero modifier, the adjective chosen by
// rarity and modifier: its form for the first of the item's tags it has (else its first form),
// with "@" replaced by that text; a "#" becomes a generated name. Items whose material form has no
// "@" use their singular form alone.
std::wstring World::createItemName(const Item &item)
{
    if (item.category == ItemCategoryFormula) {
        Item product = item;
        product.category = (unsigned char)product.formulaCategory;
        product.formulaCategory = ItemCategoryNone;
        return std::wstring(L"Formula: ") + createItemName(product);
    }
    if (item.category == ItemCategoryPet)
        return std::wstring(L"Pet: ") + creatureKey(item.id);
    if (item.category == ItemCategoryQuestItem)
        return randomName(item.formulaCategory, item.modifier) + L"'s " +
               m_speech.getForm(itemKey(item.category, item.id), L"singular");
    bool plain = item.category == ItemCategoryConsumable || item.category == ItemCategoryCandle ||
                 item.category == ItemCategoryLeftovers;
    if (plain)
        return m_speech.getForm(itemKey(item.category, item.id), L"singular");

    // Counted and never read: the spirits per spirit material.
    int spiritCounts[4];
    for (int i = 0; i < 4; i++)
        spiritCounts[i] = 0;
    for (int i = 0; i < item.spiritCount; i++) {
        if (item.spirits[i].material >= ItemMaterialFireSpirit)
            spiritCounts[item.spirits[i].material - ItemMaterialFireSpirit]++;
    }
    std::wstringstream stream;
    std::wstring material = m_speech.getForm(materialKey(item.material), L"item");
    size_t at = material.find(L'@');
    if (at == std::wstring::npos) {
        return m_speech.getForm(itemKey(item.category, item.id), L"singular");
    } else {
        Speech::Name entry = m_speech.getName(itemKey(item.category, item.id));
        std::wstring singular = entry.forms[L"singular"];
        Speech::Name adjective;
        material.replace(at, 1, singular);
        if (item.modifier != 0) {
            switch (item.rarity) {
            case ItemRarityWhite:
                switch (item.modifier % ItemNameModifierCount) {
                case ItemNameWhiteShabby:
                    adjective = m_speech.getName(L"item:shabby");
                    break;
                case ItemNameWhitePlain:
                    adjective = m_speech.getName(L"item:plain");
                    break;
                case ItemNameWhiteBattered:
                    adjective = m_speech.getName(L"item:battered");
                    break;
                case ItemNameWhiteArtless:
                    adjective = m_speech.getName(L"item:artless");
                    break;
                case ItemNameWhiteUnwieldy:
                    adjective = m_speech.getName(L"item:unwieldy");
                    break;
                case ItemNameWhiteUsed:
                    adjective = m_speech.getName(L"item:used");
                    break;
                case ItemNameWhiteDusty:
                    adjective = m_speech.getName(L"item:dusty");
                    break;
                case ItemNameWhiteScratched:
                    adjective = m_speech.getName(L"item:scratched");
                    break;
                case ItemNameWhiteWorn:
                    adjective = m_speech.getName(L"item:worn");
                    break;
                case ItemNameWhiteCommon:
                    adjective = m_speech.getName(L"item:common");
                    break;
                }
                break;
            case ItemRarityGreen:
                switch (item.modifier % ItemNameModifierCount) {
                case ItemNameGreenAdjusted:
                    adjective = m_speech.getName(L"item:adjusted");
                    break;
                case ItemNameGreenBalanced:
                    adjective = m_speech.getName(L"item:balanced");
                    break;
                case ItemNameGreenBattletested:
                    adjective = m_speech.getName(L"item:battletested");
                    break;
                case ItemNameGreenGood:
                    adjective = m_speech.getName(L"item:good");
                    break;
                case ItemNameGreenHandmade:
                    adjective = m_speech.getName(L"item:handmade");
                    break;
                case ItemNameGreenFair:
                    adjective = m_speech.getName(L"item:fair");
                    break;
                case ItemNameGreenNeat:
                    adjective = m_speech.getName(L"item:neat");
                    break;
                case ItemNameGreenClean:
                    adjective = m_speech.getName(L"item:clean");
                    break;
                case ItemNameGreenUndamaged:
                    adjective = m_speech.getName(L"item:undamaged");
                    break;
                case ItemNameGreenFlawless:
                    adjective = m_speech.getName(L"item:flawless");
                    break;
                }
                break;
            case ItemRarityBlue:
                switch (item.modifier % ItemNameModifierCount) {
                case ItemNameBlueExceptional:
                    adjective = m_speech.getName(L"item:exceptional");
                    break;
                case ItemNameBluePolished:
                    adjective = m_speech.getName(L"item:polished");
                    break;
                case ItemNameBlueExtraordinary:
                    adjective = m_speech.getName(L"item:extraordinary");
                    break;
                case ItemNameBlueExquisite:
                    adjective = m_speech.getName(L"item:exquisite");
                    break;
                case ItemNameBlueSuperb:
                    adjective = m_speech.getName(L"item:superb");
                    break;
                case ItemNameBlueUnique:
                    adjective = m_speech.getName(L"item:unique");
                    break;
                case ItemNameBlueHandsome:
                    adjective = m_speech.getName(L"item:handsome");
                    break;
                case ItemNameBlueGrand:
                    adjective = m_speech.getName(L"item:grand");
                    break;
                case ItemNameBlueMagic:
                    adjective = m_speech.getName(L"item:magic");
                    break;
                case ItemNameBlueDecorated:
                    adjective = m_speech.getName(L"item:decorated");
                    break;
                }
                break;
            case ItemRarityPurple:
                switch (item.modifier % ItemNameModifierCount) {
                case ItemNamePurpleExceptional:
                    adjective = m_speech.getName(L"item:exceptional2");
                    break;
                case ItemNamePurplePolished:
                    adjective = m_speech.getName(L"item:polished2");
                    break;
                case ItemNamePurpleExtraordinary:
                    adjective = m_speech.getName(L"item:extraordinary2");
                    break;
                case ItemNamePurpleExquisite:
                    adjective = m_speech.getName(L"item:exquisite2");
                    break;
                case ItemNamePurpleSuperb:
                    adjective = m_speech.getName(L"item:superb2");
                    break;
                case ItemNamePurpleUnique:
                    adjective = m_speech.getName(L"item:unique2");
                    break;
                case ItemNamePurpleHandsome:
                    adjective = m_speech.getName(L"item:handsome2");
                    break;
                case ItemNamePurpleGrand:
                    adjective = m_speech.getName(L"item:grand2");
                    break;
                case ItemNamePurpleMagic:
                    adjective = m_speech.getName(L"item:magic2");
                    break;
                case ItemNamePurpleDecorated:
                    adjective = m_speech.getName(L"item:decorated2");
                    break;
                }
                break;
            case ItemRarityYellow:
                switch (item.modifier % ItemNameModifierCount) {
                case ItemNameYellowBrilliant:
                    adjective = m_speech.getName(L"item:brilliant");
                    break;
                case ItemNameYellowShining:
                    adjective = m_speech.getName(L"item:shining");
                    break;
                case ItemNameYellowMagnificent:
                    adjective = m_speech.getName(L"item:magnificent");
                    break;
                case ItemNameYellowSublime:
                    adjective = m_speech.getName(L"item:sublime");
                    break;
                case ItemNameYellowPompous:
                    adjective = m_speech.getName(L"item:pompous");
                    break;
                case ItemNameYellowGlorious:
                    adjective = m_speech.getName(L"item:glorious");
                    break;
                case ItemNameYellowSplendid:
                    adjective = m_speech.getName(L"item:splendid");
                    break;
                case ItemNameYellowFamous:
                    adjective = m_speech.getName(L"item:famous");
                    break;
                case ItemNameYellowLegendary:
                    adjective = m_speech.getName(L"item:legendary");
                    break;
                case ItemNameYellowFabulous:
                    adjective = m_speech.getName(L"item:fabulous");
                    break;
                }
                break;
            }
            bool done = false;
            for (std::list<std::wstring>::iterator tag = entry.tags.begin(); tag != entry.tags.end(); ++tag) {
                if (adjective.forms.find(*tag) == adjective.forms.end())
                    continue;
                std::wstring form = adjective.forms[*tag];
                size_t pos = form.find(L'@');
                if (pos != std::wstring::npos) {
                    form.replace(pos, 1, material);
                    material = form;
                    done = true;
                }
                pos = material.find(L'#');
                // Item modifier hash selects the race used for the material name; multiplier 7 and modulus 11.
                if (pos != std::wstring::npos)
                    material.replace(pos, 1, randomName(item.modifier, item.modifier * 7 % 11));
                break;
            }
            if (!done && adjective.forms.size() != 0) {
                std::wstring form = adjective.forms.begin()->second;
                size_t pos = form.find(L'@');
                if (pos != std::wstring::npos) {
                    form.replace(pos, 1, material);
                    material = form;
                }
                pos = material.find(L'#');
                if (pos != std::wstring::npos)
                    material.replace(pos, 1, randomName(item.modifier, item.modifier * 7 % 11));
            }
        }
        stream << material;
    }
    return stream.str();
}

// Lights the columns: the second and third colour bytes of each open block carry its light. Each
// column from the top: full light (255) under the open sky, none from the first solid block down.
// Then sixteen rounds over the rectangle and its margin: an open block not fully lit takes 85 % of
// the brightest of its six neighbours (World::cubeLight reads the third byte, at least 5), kept in
// the second byte and copied to the third after the round. Finally the open blocks of the
// rectangle itself keep their light in the first byte.
void World::computeLight(int x0, int y0, int x1, int y1, int margin, Zone *zone)
{
    for (int x = x0 - margin; x < x1 + margin; x++) {
        for (int y = y0 - margin; y < y1 + margin; y++) {
            Field *field = getField(x, y, zone);
            if (!field)
                continue;
            bool sky = true;
            for (int i = field->m_size - 1; i >= 0; i--) {
                Block &block = field->getMutableBlock(i);
                if (block.isSolid())
                    sky = false;
                else if (sky)
                    block.color = plasma::Vector<3, unsigned char>(block.color[0], 255, 255);
                else
                    block.color = plasma::Vector<3, unsigned char>(block.color[0], 0, 0);
            }
        }
    }
    for (int round = 0; round < 16; round++) {
        for (int x = x0 - margin; x < x1 + margin; x++) {
            for (int y = y0 - margin; y < y1 + margin; y++) {
                Field *field = getField(x, y, zone);
                if (!field)
                    continue;
                for (int i = 0; i < field->m_size; i++) {
                    Block &block = field->getMutableBlock(i);
                    if (block.isSolid() || block.color[2] == 255)
                        continue;
                    int z = field->m_baseZ + i;
                    int light = 0;
                    int neighbour = cubeLight(x - 1, y, z, zone, 2);
                    if (neighbour > light)
                        light = neighbour;
                    if (light < 255) {
                        neighbour = cubeLight(x + 1, y, z, zone, 2);
                        if (neighbour > light)
                            light = neighbour;
                    }
                    if (light < 255) {
                        neighbour = cubeLight(x, y - 1, z, zone, 2);
                        if (neighbour > light)
                            light = neighbour;
                    }
                    if (light < 255) {
                        neighbour = cubeLight(x, y + 1, z, zone, 2);
                        if (neighbour > light)
                            light = neighbour;
                    }
                    if (light < 255) {
                        neighbour = cubeLight(x, y, z - 1, zone, 2);
                        if (neighbour > light)
                            light = neighbour;
                    }
                    if (light < 255) {
                        neighbour = cubeLight(x, y, z + 1, zone, 2);
                        if (neighbour > light)
                            light = neighbour;
                    }
                    block.color[1] = light * 85 / 100;
                }
            }
        }
        for (int x = x0 - margin; x < x1 + margin; x++) {
            for (int y = y0 - margin; y < y1 + margin; y++) {
                Field *field = getField(x, y, zone);
                if (!field)
                    continue;
                for (int i = 0; i < field->m_size; i++) {
                    Block &block = field->getMutableBlock(i);
                    if (!block.isSolid())
                        block.color[2] = block.color[1];
                }
            }
        }
    }
    for (int x = x0; x < x1; x++) {
        for (int y = y0; y < y1; y++) {
            Field *field = getField(x, y, zone);
            if (!field)
                continue;
            for (int i = 0; i < field->m_size; i++) {
                Block &block = field->getMutableBlock(i);
                if (!block.isSolid())
                    block.color[0] = block.color[2];
            }
        }
    }
}

// Queues a particle burst for each spirit of the item whose material has bit 7 set (a glowing
// spirit), coloured by the material: more particles the larger the scale, ten more and twice as
// fast when strong. createSpiritParticles has the same glowing-material packet loop.
void World::createSpiritParticles(const plasma::Vector<3, Fixed> &position,
                                  const plasma::Vector<3, float> &acceleration, float scale, bool strong,
                                  const Item &item, ServerUpdates *updates)
{
    for (int i = 0; i < item.spiritCount; i++) {
        unsigned char material = item.spirits[i].material;
        if (material < ItemMaterialFireSpirit)
            continue;
        ServerUpdates::ParticlePacket particles;
        particles.position = position;
        particles.acceleration = acceleration;
        particles.size = 0.1f;
        particles.count = (int)(scale * 3.0f + 1.0f);
        particles.type = ParticleGlow;
        particles.color = materialColor(material, plasma::Vector<4, float>(1.0f, 1.0f, 1.0f, 1.0f), scale * 0.5f);
        if (strong) {
            particles.count += 10;
            particles.acceleration *= 2.0f;
        }
        updates->particles.push_back(particles);
    }
}

// The level of the region record whose shape contains the column, 1 outside every record. The
// record is looked up per 2048 blocks.
int World::levelAt(Fixed x, Fixed y)
{
    Region::Record *record = getRecord((int)x / 2048, (int)Fixed((long long)(y.raw / 2048.0)));
    if (record && record->kind && record->shapeDistance(x, y) <= 1.0f)
        return record->level;
    return 1;
}

// The dictionary key of a creature type, or L"" (the release keeps these in Speech).
std::wstring World::creatureKey(int id)
{
    std::map<int, std::wstring>::iterator it = m_creatureKeys.find(id);
    if (it == m_creatureKeys.end())
        return L"";
    return it->second;
}

// World locks protect regions and zones.

// Clears a box of size blocks from (x, y, z) to black blocks of type 0xc0 (air with
// BlockFlagInterior and BlockFlagUnderwater: kept clear by later fills). With rough walls, noise
// widens each side of the box by a block here and there. StructureBuilder::cutBox clears the same
// kind of block box.
void World::cutBox(int x, int y, int z, const plasma::Vector<3, int> &size, Zone *zone, bool rough)
{
    for (int i = 0; i < size[0]; i++) {
        for (int j = 0; j < size[1]; j++) {
            for (int k = size[2] - 1; k >= 0; k--) {
                int px = x + i;
                int py = y + j;
                int pz = z + k;
                setBlock(px, py, pz,
                         Block(plasma::Vector<3, unsigned char>(0, 0, 0),
                               (1 << BlockFlagInterior) | (1 << BlockFlagUnderwater)),
                         zone);
                if (!rough)
                    // Rough-wall noise: swap the horizontal axes for the two wall orientations; 13/0.1 axis mix, 0.2
                    // vertical frequency.
                    continue;
                if (i == 0 && noise(px * 13 + py * 0.1, pz * 0.2) > 0.5f)
                    setBlock(px - 1, py, pz,
                             Block(plasma::Vector<3, unsigned char>(0, 0, 0),
                                   (1 << BlockFlagInterior) | (1 << BlockFlagUnderwater)),
                             zone);
                if (j == 0 && noise(py * 13 + px * 0.1, pz * 0.2) > 0.5f)
                    setBlock(px, py - 1, pz,
                             Block(plasma::Vector<3, unsigned char>(0, 0, 0),
                                   (1 << BlockFlagInterior) | (1 << BlockFlagUnderwater)),
                             zone);
                if (i == size[0] - 1 && noise(px * 13 + py * 0.1, pz * 0.2) > 0.5f)
                    setBlock(px + 1, py, pz,
                             Block(plasma::Vector<3, unsigned char>(0, 0, 0),
                                   (1 << BlockFlagInterior) | (1 << BlockFlagUnderwater)),
                             zone);
                if (j == size[1] - 1 && noise(py * 13 + px * 0.1, pz * 0.2) > 0.5f)
                    setBlock(px, py + 1, pz,
                             Block(plasma::Vector<3, unsigned char>(0, 0, 0),
                                   (1 << BlockFlagInterior) | (1 << BlockFlagUnderwater)),
                             zone);
            }
        }
    }
}

// Drops an item into its loaded zone, resting on solid ground, with random rotation and a lifetime from 500 to 799. Stamps it with the world counter and marks the zone changed. The supplied rotation is overwritten.
void World::dropItem(const Item &item, const plasma::Vector<3, Fixed> &position, float rotation, float scale,
                     std::set<Zone *> *changedZones)
{
    Zone::ZoneItem drop;
    drop.position = position;
    drop.rotation = rotation;
    drop.scale *= scale;
    drop.dropTime = rand() % 300 + 500;
    drop.dropDay = m_day;
    Zone *zone = this->zone((int)drop.position[0] / BlocksPerZone, (int)drop.position[1] / BlocksPerZone);
    if (!zone)
        return;
    drop.item = item;
    drop.rotation = (float)rand() * 360.0f / float(RAND_MAX);
    int z = toBlock(drop.position[2]);
    int y = (int)drop.position[1];
    int x = (int)drop.position[0];
    while (!getBlock(x, y, z, 0).isSolid())
        z--;
    drop.position[2] = Fixed(z + 1);
    zone->m_items.push_back(drop);
    changedZones->insert(zone);
}

namespace {
const int LeftoversRarityRollRange = 1000;
const int LeftoversRarityBias = 20;
const int LeftoversWhiteUpperBound = 700;
const int LeftoversGreenUpperBound = 950;
const int LeftoversBlueUpperBound = 999;
}

// What a killed creature leaves behind, each drop landing up to a block away along x and y, turned
// at random: one time in four (always for tinted enemies) a piece of what it wears; for enemies
// and vases one time in ten a weapon or armour around its level, and then one time in five pet
// food; one to three ingredients by its race; for enemies and vases one time in fifty a formula,
// and coins by its level (creatures with AppearanceFlagHandAttacks or AppearanceFlagBiteAttack
// drop a category-14 item instead, one time in twenty); and for enemies everything they carry but
// life potions. Players, friendly NPCs and creatures with AppearanceFlagSummoned drop nothing.
// DropLoot has the same death-drop role.
void World::dropLoot(Creature *creature, std::set<Zone *> *changedZones)
{
    if (creature->m_entityData.hostilityType == HostilityPlayer ||
        creature->m_entityData.hostilityType == HostilityFriendlyNpc ||
        (creature->m_entityData.appearance.appearanceFlags & (1 << AppearanceFlagSummoned)))
        return;
    rand(); // drawn and discarded

    std::vector<Item *> worn;
    if (creature->m_entityData.equipment.weaponLeft.category != ItemCategoryNone)
        worn.push_back(&creature->m_entityData.equipment.weaponLeft);
    if (creature->m_entityData.equipment.weaponRight.category != ItemCategoryNone)
        worn.push_back(&creature->m_entityData.equipment.weaponRight);
    if (creature->m_entityData.equipment.chest.category != ItemCategoryNone)
        worn.push_back(&creature->m_entityData.equipment.chest);
    if (creature->m_entityData.equipment.shoulder.category != ItemCategoryNone)
        worn.push_back(&creature->m_entityData.equipment.shoulder);
    if (creature->m_entityData.equipment.hands.category != ItemCategoryNone)
        worn.push_back(&creature->m_entityData.equipment.hands);
    if (creature->m_entityData.equipment.feet.category != ItemCategoryNone)
        worn.push_back(&creature->m_entityData.equipment.feet);
    if (creature->m_entityData.equipment.neck.category != ItemCategoryNone)
        worn.push_back(&creature->m_entityData.equipment.neck);
    if (!worn.empty() &&
        (rand() % 4 == 0 || (creature->m_entityData.appearance.appearanceFlags & (1 << AppearanceFlagTinted)))) {
        plasma::Vector<3, Fixed> offset(plasma::Vector<3, float>(1.0f - rand() * 2.0f / float(RAND_MAX),
                                                                 1.0f - rand() * 2.0f / float(RAND_MAX), 0.0f));
        // Each offset below draws x, then y; here the original draws the index, then the rotation.
        dropItem(*worn[rand() % worn.size()], creature->m_entityData.position + offset,
                 rand() * 360.0f / float(RAND_MAX), 1.0f, changedZones);
    }

    if ((creature->m_entityData.hostilityType == HostilityEnemy || creature->m_entityData.race == RaceVase) &&
        rand() % 10 == 0) {
        int level = creature->m_entityData.level - 1 + rand() % 2;
        if (level < 1)
            level = 1;
        if (rand() % 2) {
            Item item = randomWeapon(level, randomRarity(creature->m_entityData.rarity, false), -1);
            item.varyLevel(0.05f, true);
            plasma::Vector<3, Fixed> offset(plasma::Vector<3, float>(1.0f - rand() * 2.0f / float(RAND_MAX),
                                                                     1.0f - rand() * 2.0f / float(RAND_MAX), 0.0f));
            dropItem(item, creature->m_entityData.position + offset, rand() * 360.0f / float(RAND_MAX), 1.0f,
                     changedZones);
        } else {
            Item item = randomArmor(level, randomRarity(creature->m_entityData.rarity, false), -1);
            item.varyLevel(0.05f, true);
            plasma::Vector<3, Fixed> offset(plasma::Vector<3, float>(1.0f - rand() * 2.0f / float(RAND_MAX),
                                                                     1.0f - rand() * 2.0f / float(RAND_MAX), 0.0f));
            dropItem(item, creature->m_entityData.position + offset, rand() * 360.0f / float(RAND_MAX), 1.0f,
                     changedZones);
        }
        if (rand() % 5 == 0) {
            Item food = randomPetFood(level, randomRarity(creature->m_entityData.rarity, false));
            food.varyLevel(0.05f, true);
            plasma::Vector<3, Fixed> offset(plasma::Vector<3, float>(1.0f - rand() * 2.0f / float(RAND_MAX),
                                                                     1.0f - rand() * 2.0f / float(RAND_MAX), 0.0f));
            dropItem(food, creature->m_entityData.position + offset, rand() * 360.0f / float(RAND_MAX), 1.0f,
                     changedZones);
        }
    }

    // The ingredient names are the alpha's item keys (World::World).
    int count = rand() % 3 + 1;
    for (int i = 0; i < count; i++) {
        Item item;
        float scale = 0.75f;
        item.level = 1; // one of each (a material's level is its quantity)
        switch (creature->m_entityData.race) {
        case RaceIronDeposit:
            item.category = ItemCategoryMaterial;
            item.id = MaterialItemNugget; // Nugget
            item.material = ItemMaterialIron;
            scale = 3.0f;
            break;
        case RaceSilverDeposit:
            item.category = ItemCategoryMaterial;
            item.id = MaterialItemNugget;
            item.material = ItemMaterialSilver;
            scale = 3.0f;
            break;
        case RaceGoldDeposit:
            item.category = ItemCategoryMaterial;
            item.id = MaterialItemNugget;
            item.material = ItemMaterialGold;
            scale = 3.0f;
            break;
        case RaceEmeraldDeposit:
            item.category = ItemCategoryMaterial;
            item.id = MaterialItemNugget;
            item.rarity = ItemRarityGreen;
            item.material = ItemMaterialEmerald;
            scale = 3.0f;
            break;
        case RaceSapphireDeposit:
            item.category = ItemCategoryMaterial;
            item.id = MaterialItemNugget;
            item.rarity = ItemRarityBlue;
            item.material = ItemMaterialSapphire;
            scale = 3.0f;
            break;
        case RaceRubyDeposit:
            item.category = ItemCategoryMaterial;
            item.id = MaterialItemNugget;
            item.rarity = ItemRarityPurple;
            item.material = ItemMaterialRuby;
            scale = 3.0f;
            break;
        case RaceDiamondDeposit:
            item.category = ItemCategoryMaterial;
            item.id = MaterialItemNugget;
            item.rarity = ItemRarityYellow;
            item.material = ItemMaterialDiamond;
            scale = 3.0f;
            break;
        case RaceSandstoneDeposit:
            item.category = ItemCategoryMaterial;
            item.id = MaterialItemNugget;
            item.material = ItemMaterialSandstone;
            scale = 0.8f;
            break;
        case RaceIceCrystalDeposit:
            item.category = ItemCategoryMaterial;
            item.id = MaterialItemCrystal; // Crystal
            item.material = ItemMaterialIce;
            scale = 0.8f;
            break;
        case RaceCactus:
            item.category = ItemCategoryMaterial;
            item.id = MaterialItemPricklyPear; // PricklyPear
            scale = 0.8f;
            break;
        case RaceBush:
        case RaceSnowBush:
        case RaceScrub:
        case RaceFireScrub:
            if (rand() % 2) {
                item.category = ItemCategoryMaterial;
                item.id = MaterialItemLog; // Log
                item.material = ItemMaterialWood;
            } else {
                item.category = ItemCategoryMaterial;
                item.id = MaterialItemFiber; // Fiber
                item.material = ItemMaterialPlant;
            }
            scale = 0.8f;
            break;
        case RaceSnowBerryBush:
            item.category = ItemCategoryMaterial;
            item.id = MaterialItemSnowBerry; // SnowBerry
            item.material = 0;
            scale = 0.8f;
            break;
        case RaceCobwebScrub:
            item.category = ItemCategoryMaterial;
            item.id = MaterialItemCobweb; // Cobweb
            item.material = 0;
            scale = 0.8f;
            break;
        case RaceCottonPlant:
            item.category = ItemCategoryMaterial;
            item.id = MaterialItemCapsule; // Capsule
            item.material = ItemMaterialCotton;
            scale = 0.8f;
            break;
        case RaceRadishCreature:
            item.category = ItemCategoryMaterial;
            item.id = MaterialItemRadishSlice; // RadishSlice
            scale = 0.8f;
            break;
        case RaceOnionling:
            item.category = ItemCategoryMaterial;
            item.id = MaterialItemOnionSlice; // OnionSlice
            scale = 0.8f;
            break;
        case RaceGinseng:
            item.category = ItemCategoryMaterial;
            item.id = MaterialItemGinsengRoot; // GinsengRoot
            scale = 0.8f;
            break;
        case RaceParrot:
            item.category = ItemCategoryMaterial;
            item.id = MaterialItemFeather; // Feather
            item.material = ItemMaterialParrot;
            scale = 0.8f;
            break;
        }
        if (item.category == ItemCategoryNone)
            break;
        plasma::Vector<3, Fixed> offset(plasma::Vector<3, float>(1.0f - rand() * 2.0f / float(RAND_MAX),
                                                                 1.0f - rand() * 2.0f / float(RAND_MAX), 0.0f));
        dropItem(item, creature->m_entityData.position + offset, rand() * 360.0f / float(RAND_MAX), scale,
                 changedZones);
    }

    if (creature->m_entityData.hostilityType == HostilityEnemy || creature->m_entityData.race == RaceVase) {
        if (rand() % 50 == 0) {
            plasma::Vector<3, Fixed> offset(plasma::Vector<3, float>(1.0f - rand() * 2.0f / float(RAND_MAX),
                                                                     1.0f - rand() * 2.0f / float(RAND_MAX), 0.0f));
            // The original draws the rotation first, then randomRarity and randomFormula draw.
            dropItem(
                randomFormula(creature->m_entityData.level, randomRarity(creature->m_entityData.rarity + 1, false)),
                creature->m_entityData.position + offset, rand() * 360.0f / float(RAND_MAX), 0.75f, changedZones);
        }
        if (!(creature->m_entityData.appearance.appearanceFlags & (1 << AppearanceFlagHandAttacks)) &&
            !(creature->m_entityData.appearance.appearanceFlags & (1 << AppearanceFlagBiteAttack))) {
            // Coins worth ten to thirty times the creature's power, ten times that for tinted enemies, in
            // copper, silver and gold stacks (100 of each make one of the next).
            float value = itemPowerScale((float)creature->m_entityData.level, creature->m_entityData.rarity) * 10.0f *
                          (rand() * 2.0f / float(RAND_MAX) + 1.0f);
            if (creature->m_entityData.appearance.appearanceFlags & (1 << AppearanceFlagTinted))
                value *= 10.0f;
            int coins = (int)value;
            int copper = coins % 100;
            int silver = coins / 100 % 100;
            int gold = coins / 100 / 100;
            if (copper) {
                Item coin;
                coin.category = ItemCategoryCoin;
                coin.id = CoinCoin;
                coin.material = ItemMaterialCopper;
                coin.level = copper;
                plasma::Vector<3, Fixed> offset(plasma::Vector<3, float>(1.0f - rand() * 2.0f / float(RAND_MAX),
                                                                         1.0f - rand() * 2.0f / float(RAND_MAX), 0.0f));
                dropItem(coin, creature->m_entityData.position + offset, rand() * 360.0f / float(RAND_MAX), 0.75f,
                         changedZones);
            }
            if (silver) {
                Item coin;
                coin.category = ItemCategoryCoin;
                coin.id = CoinCoin;
                coin.material = ItemMaterialSilver;
                coin.level = silver;
                plasma::Vector<3, Fixed> offset(plasma::Vector<3, float>(1.0f - rand() * 2.0f / float(RAND_MAX),
                                                                         1.0f - rand() * 2.0f / float(RAND_MAX), 0.0f));
                dropItem(coin, creature->m_entityData.position + offset, rand() * 360.0f / float(RAND_MAX), 0.75f,
                         changedZones);
            }
            if (gold) {
                Item coin;
                coin.category = ItemCategoryCoin;
                coin.id = CoinCoin;
                coin.material = ItemMaterialGold;
                coin.level = gold;
                plasma::Vector<3, Fixed> offset(plasma::Vector<3, float>(1.0f - rand() * 2.0f / float(RAND_MAX),
                                                                         1.0f - rand() * 2.0f / float(RAND_MAX), 0.0f));
                dropItem(coin, creature->m_entityData.position + offset, rand() * 360.0f / float(RAND_MAX), 0.75f,
                         changedZones);
            }
        } else {
            // Returns rarity one to three, with better odds for the creature's rarity bonus and tinted enemies.
            int roll = rand() % LeftoversRarityRollRange + creature->m_entityData.rarity * LeftoversRarityBias;
            int rarity;
            if (roll < LeftoversWhiteUpperBound)
                rarity = ItemRarityWhite;
            else if (roll < LeftoversGreenUpperBound)
                rarity = ItemRarityGreen;
            else if (roll < LeftoversBlueUpperBound)
                rarity = ItemRarityBlue;
            else
                rarity = ItemRarityPurple;
            if (creature->m_entityData.appearance.appearanceFlags & (1 << AppearanceFlagTinted)) {
                rarity++;
                if (rarity > ItemRarityPurple)
                    rarity = ItemRarityPurple;
            }
            if (rand() % 20 == 0 && rarity != ItemRarityWhite) {
                Item item;
                item.category = ItemCategoryLeftovers;
                item.modifier = rand();
                item.level = creature->m_entityData.level;
                item.rarity = rarity;
                plasma::Vector<3, Fixed> offset(plasma::Vector<3, float>(1.0f - rand() * 2.0f / float(RAND_MAX),
                                                                         1.0f - rand() * 2.0f / float(RAND_MAX), 0.0f));
                dropItem(item, creature->m_entityData.position + offset, rand() * 360.0f / float(RAND_MAX), 0.75f,
                         changedZones);
            }
        }
    }

    if (creature->m_entityData.hostilityType == HostilityEnemy && !creature->m_inventory.m_inventoryTabs.empty()) {
        for (int i = 0; i < (int)creature->m_inventory.m_inventoryTabs.size(); i++) {
            for (std::vector<ItemStack>::iterator stack = creature->m_inventory.m_inventoryTabs[i].begin();
                 stack != creature->m_inventory.m_inventoryTabs[i].end(); ++stack) {
                if (stack->item.category == ItemCategoryNone || stack->quantity == 0)
                    continue;
                for (int j = 0; j < stack->quantity; j++) {
                    if (stack->item.category == ItemCategoryConsumable && stack->item.id == ConsumableLifePotion)
                        continue;
                    plasma::Vector<3, Fixed> offset(plasma::Vector<3, float>(
                        1.0f - rand() * 2.0f / float(RAND_MAX), 1.0f - rand() * 2.0f / float(RAND_MAX), 0.0f));
                    dropItem(stack->item, creature->m_entityData.position + offset, rand() * 360.0f / float(RAND_MAX),
                             0.75f, changedZones);
                }
            }
        }
    }
}

// Sets the blocks of a horizontal circle of the radius around (x, y) at height z (around the
// block's centre when centered), except blocks with BlockFlagInterior. When tinted, each block's
// colour is blended towards green (50, 120, 60) by colorBlend; the squared-radius test bounds each
// SetBlock.
void World::fillCircle(int x, int y, int z, float radius, const Block &block, Zone *zone, bool centered, bool tinted)
{
    double centerX = x;
    double centerY = y;
    if (centered) {
        centerX += 0.5;
        centerY += 0.5;
    }
    for (int i = (int)(x - radius - 1.0f); i <= x + radius + 1.0f; i++) {
        for (int j = (int)(y - radius - 1.0f); j <= y + radius + 1.0f; j++) {
            if ((i - centerX) * (i - centerX) + (j - centerY) * (j - centerY) <= radius * radius) {
                if (getBlock(i, j, z, zone).type & (1 << BlockFlagInterior))
                    continue;
                if (tinted) {
                    plasma::Vector<3, float> color(block.color);
                    plasma::Vector<3, float> tint =
                        (plasma::Vector<3, float>(50.0f, 120.0f, 60.0f) - color) * colorBlend(i, j, z, zone);
                    tint += color;
                    setBlock(i, j, z, Block(plasma::Vector<3, unsigned char>(tint), block.type), zone);
                } else {
                    setBlock(i, j, z, block, zone);
                }
            }
        }
    }
}

// FillCircle for a sphere of the radius around (x, y, z).
void World::fillSphere(int x, int y, int z, float radius, const Block &block, Zone *zone, bool centered, bool tinted)
{
    double centerX = x;
    double centerY = y;
    double centerZ = z;
    if (centered) {
        centerX += 0.5;
        centerY += 0.5;
        centerZ += 0.5;
    }
    for (int i = (int)(x - radius - 1.0f); i <= x + radius + 1.0f; i++) {
        for (int j = (int)(y - radius - 1.0f); j <= y + radius + 1.0f; j++) {
            for (int k = (int)(z - radius - 1.0f); k <= z + radius + 1.0f; k++) {
                if ((i - centerX) * (i - centerX) + (j - centerY) * (j - centerY) + (k - centerZ) * (k - centerZ) <=
                    radius * radius) {
                    if (getBlock(i, j, k, zone).type & (1 << BlockFlagInterior))
                        continue;
                    if (tinted) {
                        plasma::Vector<3, float> color(block.color);
                        plasma::Vector<3, float> tint =
                            (plasma::Vector<3, float>(50.0f, 120.0f, 60.0f) - color) * colorBlend(i, j, k, zone);
                        tint += color;
                        setBlock(i, j, k, Block(plasma::Vector<3, unsigned char>(tint), block.type), zone);
                    } else {
                        setBlock(i, j, k, block, zone);
                    }
                }
            }
        }
    }
}

void World::enterRegions()
{
    EnterCriticalSection(&m_regionLock);
}

void World::enterZones()
{
    EnterCriticalSection(&m_zoneLock);
}

void World::leaveRegions()
{
    LeaveCriticalSection(&m_regionLock);
}

void World::leaveZones()
{
    LeaveCriticalSection(&m_zoneLock);
}

#ifndef CUBE_SERVER
// The recipe of an item: the item itself as the result, material items as ingredients and the
// workstation to craft it at (none for some). An item of rarity 1..4 also needs two nuggets of
// emerald, sapphire, ruby or diamond. Always true.
bool World::findFormula(const Item &item, Formula &formula)
{
    formula.result = item;
    Ingredient ingredient;
    ingredient.item.level = 1;
    ingredient.item.category = ItemCategoryMaterial;
    switch (item.category) {
    case ItemCategoryWeapon:
        switch (item.id) {
        // Recipe table: material quantities are selected by weapon, armor and accessory kind.
        case WeaponSword:
            ingredient.quantity = 8;
            ingredient.item.id = MaterialItemCube;
            ingredient.item.material = ItemMaterialIron;
            formula.ingredients.push_back(ingredient);
            formula.workstationRequired = WorkstationAnvil;
            break;
        case WeaponAxe:
            ingredient.quantity = 8;
            ingredient.item.id = MaterialItemCube;
            ingredient.item.material = ItemMaterialIron;
            formula.ingredients.push_back(ingredient);
            formula.workstationRequired = WorkstationAnvil;
            break;
        case WeaponMace:
            ingredient.quantity = 8;
            ingredient.item.id = MaterialItemCube;
            ingredient.item.material = ItemMaterialIron;
            formula.ingredients.push_back(ingredient);
            formula.workstationRequired = WorkstationAnvil;
            break;
        case WeaponDagger:
            ingredient.quantity = 8;
            ingredient.item.id = MaterialItemCube;
            ingredient.item.material = ItemMaterialIron;
            formula.ingredients.push_back(ingredient);
            formula.workstationRequired = WorkstationAnvil;
            break;
        case WeaponShield:
            ingredient.quantity = 8;
            ingredient.item.id = MaterialItemCube;
            ingredient.item.material = ItemMaterialIron;
            formula.ingredients.push_back(ingredient);
            formula.workstationRequired = WorkstationAnvil;
            break;
        case WeaponBow:
            ingredient.quantity = 15;
            ingredient.item.id = MaterialItemCube;
            ingredient.item.material = ItemMaterialWood;
            formula.ingredients.push_back(ingredient);
            ingredient.quantity = 1;
            ingredient.item.id = MaterialItemYarn;
            ingredient.item.material = ItemMaterialLinen;
            formula.ingredients.push_back(ingredient);
            formula.workstationRequired = WorkstationWorkbench;
            break;
        case WeaponCrossbow:
            ingredient.quantity = 6;
            ingredient.item.id = MaterialItemCube;
            ingredient.item.material = ItemMaterialIron;
            formula.ingredients.push_back(ingredient);
            ingredient.quantity = 9;
            ingredient.item.id = MaterialItemCube;
            ingredient.item.material = ItemMaterialWood;
            formula.ingredients.push_back(ingredient);
            ingredient.quantity = 1;
            ingredient.item.id = MaterialItemYarn;
            ingredient.item.material = ItemMaterialLinen;
            formula.ingredients.push_back(ingredient);
            formula.workstationRequired = WorkstationWorkbench;
            break;
        case WeaponBoomerang:
            ingredient.quantity = 16;
            ingredient.item.id = MaterialItemCube;
            ingredient.item.material = ItemMaterialWood;
            formula.ingredients.push_back(ingredient);
            formula.workstationRequired = WorkstationWorkbench;
            break;
        case WeaponStaff:
            ingredient.quantity = 16;
            ingredient.item.id = MaterialItemCube;
            ingredient.item.material = ItemMaterialWood;
            formula.ingredients.push_back(ingredient);
            formula.workstationRequired = WorkstationWorkbench;
            break;
        case WeaponWand:
            ingredient.quantity = 16;
            ingredient.item.id = MaterialItemCube;
            ingredient.item.material = ItemMaterialWood;
            formula.ingredients.push_back(ingredient);
            formula.workstationRequired = WorkstationWorkbench;
            break;
        case WeaponBracelet:
            ingredient.quantity = 8;
            ingredient.item.id = MaterialItemCube;
            ingredient.item.material = item.material;
            formula.workstationRequired = WorkstationAnvil;
            formula.ingredients.push_back(ingredient);
            break;
        case WeaponFist:
            ingredient.quantity = 6;
            ingredient.item.id = MaterialItemCube;
            ingredient.item.material = ItemMaterialIron;
            formula.ingredients.push_back(ingredient);
            ingredient.quantity = 2;
            ingredient.item.id = MaterialItemYarn;
            ingredient.item.material = ItemMaterialCotton;
            formula.ingredients.push_back(ingredient);
            formula.workstationRequired = WorkstationAnvil;
            break;
        case WeaponLongsword:
            ingredient.quantity = 16;
            ingredient.item.id = MaterialItemCube;
            ingredient.item.material = ItemMaterialIron;
            formula.ingredients.push_back(ingredient);
            formula.workstationRequired = WorkstationAnvil;
            break;
        case WeaponGreatsword:
            ingredient.quantity = 16;
            ingredient.item.id = MaterialItemCube;
            ingredient.item.material = ItemMaterialIron;
            formula.ingredients.push_back(ingredient);
            formula.workstationRequired = WorkstationAnvil;
            break;
        case WeaponGreataxe:
            ingredient.quantity = 16;
            ingredient.item.id = MaterialItemCube;
            ingredient.item.material = ItemMaterialIron;
            formula.ingredients.push_back(ingredient);
            formula.workstationRequired = WorkstationAnvil;
            break;
        case WeaponGreatmace:
            ingredient.quantity = 16;
            ingredient.item.id = MaterialItemCube;
            ingredient.item.material = ItemMaterialIron;
            formula.ingredients.push_back(ingredient);
            formula.workstationRequired = WorkstationAnvil;
            break;
        }
        break;
    // Armour: iron cubes at workstation 4, or yarn of its material at 6.
    case ItemCategoryChest:
        ingredient.quantity = 20;
        switch (item.material) {
        case ItemMaterialIron:
            ingredient.item.id = MaterialItemCube;
            ingredient.item.material = ItemMaterialIron;
            formula.ingredients.push_back(ingredient);
            formula.workstationRequired = WorkstationAnvil;
            break;
        default:
            ingredient.item.material = item.material;
            ingredient.item.id = MaterialItemYarn;
            formula.ingredients.push_back(ingredient);
            formula.workstationRequired = WorkstationLoom;
            break;
        }
        break;
    case ItemCategoryFeet:
        ingredient.quantity = 8;
        switch (item.material) {
        case ItemMaterialIron:
            ingredient.item.id = MaterialItemCube;
            ingredient.item.material = ItemMaterialIron;
            formula.ingredients.push_back(ingredient);
            formula.workstationRequired = WorkstationAnvil;
            break;
        default:
            ingredient.item.material = item.material;
            ingredient.item.id = MaterialItemYarn;
            formula.ingredients.push_back(ingredient);
            formula.workstationRequired = WorkstationLoom;
            break;
        }
        break;
    case ItemCategoryShoulder:
        ingredient.quantity = 8;
        switch (item.material) {
        case ItemMaterialIron:
            ingredient.item.id = MaterialItemCube;
            ingredient.item.material = ItemMaterialIron;
            formula.ingredients.push_back(ingredient);
            formula.workstationRequired = WorkstationAnvil;
            break;
        default:
            ingredient.item.material = item.material;
            ingredient.item.id = MaterialItemYarn;
            formula.ingredients.push_back(ingredient);
            formula.workstationRequired = WorkstationLoom;
            break;
        }
        break;
    case ItemCategoryHands:
        ingredient.quantity = 6;
        switch (item.material) {
        case ItemMaterialIron:
            ingredient.item.id = MaterialItemCube;
            ingredient.item.material = ItemMaterialIron;
            formula.ingredients.push_back(ingredient);
            formula.workstationRequired = WorkstationAnvil;
            break;
        default:
            ingredient.item.material = item.material;
            ingredient.item.id = MaterialItemYarn;
            formula.ingredients.push_back(ingredient);
            formula.workstationRequired = WorkstationLoom;
            break;
        }
        break;
    case ItemCategoryNeck:
        ingredient.quantity = 6;
        ingredient.item.id = MaterialItemCube;
        ingredient.item.material = item.material;
        formula.ingredients.push_back(ingredient);
        break;
    case ItemCategoryRing:
        ingredient.quantity = 3;
        ingredient.item.id = MaterialItemCube;
        ingredient.item.material = item.material;
        formula.ingredients.push_back(ingredient);
        break;
    // Refined materials.
    case ItemCategoryMaterial:
        switch (item.id) {
        case MaterialItemWaterFlask:
            ingredient.quantity = 1;
            ingredient.item.id = MaterialItemFlask;
            ingredient.item.material = ItemMaterialGlass;
            formula.ingredients.push_back(ingredient);
            formula.workstationRequired = WorkstationWater;
            break;
        case MaterialItemHeartflower:
            ingredient.item.id = MaterialItemFrozenHeartflower;
            ingredient.item.material = 0;
            ingredient.quantity = 1;
            formula.ingredients.push_back(ingredient);
            formula.workstationRequired = WorkstationCampfire;
            break;
        case MaterialItemCube:
            ingredient.quantity = 1;
            if (item.material == ItemMaterialWood) {
                ingredient.item.id = MaterialItemLog;
                formula.workstationRequired = WorkstationSaw;
            } else {
                ingredient.item.id = MaterialItemNugget;
                formula.workstationRequired = WorkstationFurnace;
            }
            ingredient.item.material = item.material;
            formula.ingredients.push_back(ingredient);
            break;
        case MaterialItemYarn:
            switch (item.material) {
            case ItemMaterialCotton:
                ingredient.item.id = MaterialItemCapsule;
                ingredient.item.material = ItemMaterialCotton;
                break;
            case ItemMaterialLinen:
                ingredient.item.id = MaterialItemFiber;
                ingredient.item.material = ItemMaterialPlant;
                break;
            case ItemMaterialSilk:
                ingredient.item.id = MaterialItemCobweb;
                ingredient.item.material = 0;
                break;
            default:
                // No recipe: only the gems below.
                goto gems;
            }
            ingredient.quantity = 1;
            formula.ingredients.push_back(ingredient);
            formula.workstationRequired = WorkstationSpinningWheel;
            break;
        }
        break;
    case ItemCategoryConsumable:
        switch (item.id) {
        case ConsumableLifePotion:
            ingredient.quantity = 1;
            ingredient.item.id = MaterialItemHeartflower;
            formula.ingredients.push_back(ingredient);
            ingredient.item.id = MaterialItemWaterFlask;
            formula.ingredients.push_back(ingredient);
            break;
        case ConsumableCactusPotion:
            ingredient.quantity = 1;
            ingredient.item.id = MaterialItemPricklyPear;
            formula.ingredients.push_back(ingredient);
            ingredient.item.id = MaterialItemFlask;
            ingredient.item.material = ItemMaterialGlass;
            formula.ingredients.push_back(ingredient);
            break;
        case ConsumableGinsengSoup:
            ingredient.item.id = MaterialItemGinsengRoot;
            ingredient.quantity = 1;
            formula.ingredients.push_back(ingredient);
            formula.workstationRequired = WorkstationCampfire;
            break;
        case ConsumableMushroomSpit:
            ingredient.quantity = 1;
            ingredient.item.id = MaterialItemMushroom;
            formula.ingredients.push_back(ingredient);
            ingredient.item.id = MaterialItemOnionSlice;
            ingredient.quantity = 1;
            formula.ingredients.push_back(ingredient);
            formula.workstationRequired = WorkstationCampfire;
            break;
        case ConsumableSnowBerryMash:
            ingredient.quantity = 1;
            ingredient.item.id = MaterialItemSnowBerry;
            formula.ingredients.push_back(ingredient);
            break;
        case ConsumablePineappleSlice:
            ingredient.quantity = 1;
            ingredient.item.id = MaterialItemPineapple;
            formula.ingredients.push_back(ingredient);
            break;
        case ConsumablePumpkinMuffin:
            ingredient.item.id = MaterialItemPumpkin;
            ingredient.quantity = 1;
            formula.ingredients.push_back(ingredient);
            formula.workstationRequired = WorkstationCampfire;
            break;
        }
        break;
    }

gems:
    switch (item.rarity) {
    case ItemRarityGreen:
        ingredient.item.rarity = ItemRarityGreen;
        ingredient.item.material = ItemMaterialEmerald;
        break;
    case ItemRarityBlue:
        ingredient.item.rarity = ItemRarityBlue;
        ingredient.item.material = ItemMaterialSapphire;
        break;
    case ItemRarityPurple:
        ingredient.item.rarity = ItemRarityPurple;
        ingredient.item.material = ItemMaterialRuby;
        break;
    case ItemRarityYellow:
        ingredient.item.rarity = ItemRarityYellow;
        ingredient.item.material = ItemMaterialDiamond;
        break;
    default:
        return true;
    }
    ingredient.quantity = 2;
    ingredient.item.id = MaterialItemNugget;
    formula.ingredients.push_back(ingredient);
    return true;
}
#endif

#ifndef CUBE_SERVER
// The sky and the light at a position. Each time of day has three sky colours: day, sunset, dusk
// and night. The landscape changes them: a hot and dry land (temperature above 0.6, humidity below
// 0.4) yellows the day's horizon, a warm and wet one away from the sea greens it and turns the day
// sky cyan, a dry one (humidity below 0.4) pales the day sky, and the fire lands redden every
// palette and light. The daylight (1 at noon, 0 at midnight: the time of day's distance from
// midnight, cubed and inverted, to the fourth power) blends the palettes: day above 0.75, day into
// sunset down to 0.6, sunset into dusk down to 0.4, dusk into night below. Under water the sky is
// a dark blue of the daylight. The front and back lights blend day and dusk above a daylight of
// 0.5 and dusk and night below; three of their colours leave the fourth component unset (the
// three-element constructor), which the blends read.
void World::lighting(const plasma::Vector<3, Fixed> &position, plasma::Vector<4, float> &skyColor0,
                     plasma::Vector<4, float> &skyColor1, plasma::Vector<4, float> &skyColor2,
                     plasma::Vector<4, float> &frontLight, plasma::Vector<4, float> &backLight,
                     plasma::Vector<4, float> &fogColor)
{
    float normalizedDistanceFromNoon = fabs(m_time * 2.0f / float(24 * World::MillisecondsPerHour) - 1.0f);
    float daylight = (float)pow(1.0f - (float)pow(normalizedDistanceFromNoon, 3.0), 4.0);
    bool underwater = (getBlock(position[0], position[1], position[2], 0).type & BlockTypeMask) == BlockWater;
    fogColor = plasma::Vector<4, float>(0.4f, 0.4f, 0.4f, 1.0f);

    // Time-of-day sky, fog and front/back light palettes, blended with biome colors.
    plasma::Vector<4, float> day0(0.1f, 0.6f, 1.0f, 1.0f);
    plasma::Vector<4, float> day1(0.01f, 0.05f, 1.0f, 1.0f);
    plasma::Vector<4, float> day2(0.01f, 0.1f, 1.0f, 1.0f);
    plasma::Vector<4, float> sunset0(1.0f, 0.9f, 0.5f, 1.0f);
    plasma::Vector<4, float> sunset1(0.2f, 0.1f, 0.8f, 1.0f);
    plasma::Vector<4, float> sunset2(1.0f, 0.9f, 0.8f, 1.0f);
    plasma::Vector<4, float> dusk0(1.0f, 0.3f, 0.3f, 1.0f);
    plasma::Vector<4, float> dusk1(0.5f, 0.0f, 0.5f, 1.0f);
    plasma::Vector<4, float> dusk2(0.5f, 0.2f, 0.6f, 1.0f);
    plasma::Vector<4, float> night0(0.05f, 0.0f, 0.1f, 1.0f);
    plasma::Vector<4, float> night1(0.0f, 0.0f, 0.1f, 1.0f);
    plasma::Vector<4, float> night2(0.0f, 0.0f, 0.1f, 1.0f);

    float temperature = blendedTemperature((int)position[0], (int)position[1]);
    float humidity = blendedHumidity((int)position[0], (int)position[1]);
    // Desert: a pale horizon.
    if (temperature > 0.6f && humidity < 0.4f) {
        float desert = (1.0f - humidity / 0.4f) * ((temperature - 0.6f) / 0.4f);
        day2 = day2 + (plasma::Vector<4, float>(0.8f, 0.7f, 0.8f, 1.0f) - day2) * desert;
    }
    // Jungle (not over the sea): a green horizon and a cyan sky.
    if (temperature > 0.3f && humidity > 0.6f) {
        float warmth = (temperature - 0.3f) / 0.7f * 4.0f;
        if (warmth > 1.0f)
            warmth = 1.0f;
        float jungle = (1.0f - oceanShare((int)position[0], (int)position[1])) * ((humidity - 0.6f) * warmth / 0.4f);
        day2 = day2 + (plasma::Vector<4, float>(0.0f, 0.7f, 0.5f, 1.0f) - day2) * jungle;
        day0 = day0 + (plasma::Vector<4, float>(0.5f, 0.9f, 1.0f, 1.0f) - day0) * jungle;
        day1 = day1 + (plasma::Vector<4, float>(0.0f, 0.6f, 0.9f, 1.0f) - day1) * jungle;
    }
    // Dry land: a paler day sky.
    if (humidity < 0.4f) {
        float dry = 1.0f - humidity / 0.4f;
        day0 = day0 + (plasma::Vector<4, float>(0.6f, 0.8f, 1.0f, 1.0f) - day0) * dry;
        day1 = day1 + (plasma::Vector<4, float>(0.1f, 0.4f, 1.0f, 1.0f) - day1) * dry;
    }
    // The fog: blue-grey in the cold.
    fogColor = plasma::Vector<4, float>(0.6f, 0.6f, 0.4f, 1.0f);
    if (temperature < 0.2f) {
        float warm = temperature / 0.2f;
        fogColor = plasma::Vector<4, float>(0.3f, 0.4f, 0.6f, 1.0f) * (1.0f - warm) +
                   plasma::Vector<4, float>(0.6f, 0.6f, 0.4f, 1.0f) * warm;
    }
    // The fire lands: red skies.
    float fire = blendedFirelands((int)position[0], (int)position[1]);
    if (fire > 0.0f) {
        day0 = day0 * (1.0f - fire) + plasma::Vector<4, float>(1.0f, 0.0f, 0.0f, 1.0f) * fire;
        sunset0 = sunset0 * (1.0f - fire) + plasma::Vector<4, float>(0.8f, 0.1f, 0.0f, 1.0f) * fire;
        dusk0 = dusk0 * (1.0f - fire) + plasma::Vector<4, float>(0.8f, 0.1f, 0.0f, 1.0f) * fire;
        day1 = day1 * (1.0f - fire) + plasma::Vector<4, float>(0.4f, 0.2f, 0.2f, 1.0f) * fire;
        sunset1 = sunset1 * (1.0f - fire) + plasma::Vector<4, float>(0.2f, 0.2f, 0.2f, 1.0f) * fire;
        dusk1 = dusk1 * (1.0f - fire) + plasma::Vector<4, float>(0.2f, 0.2f, 0.2f, 1.0f) * fire;
        day2 = day2 * (1.0f - fire) + plasma::Vector<4, float>(0.3f, 0.0f, 0.0f, 1.0f) * fire;
        sunset2 = sunset2 * (1.0f - fire) + plasma::Vector<4, float>(0.3f, 0.0f, 0.0f, 1.0f) * fire;
        dusk2 = dusk2 * (1.0f - fire) + plasma::Vector<4, float>(0.3f, 0.1f, 0.0f, 1.0f) * fire;
        night2 = night2 * (1.0f - fire) + plasma::Vector<4, float>(0.3f, 0.2f, 0.1f, 1.0f) * fire;
    }

    skyColor0 = plasma::Vector<4, float>(0.5f, 0.8f, 1.0f, 1.0f) * daylight +
                plasma::Vector<4, float>(0.0f, 0.0f, 0.2f, 1.0f) * (1.0f - daylight);
    skyColor1 = plasma::Vector<4, float>(0.0f, 0.0f, 1.0f, 1.0f) * daylight +
                plasma::Vector<4, float>(0.0f, 0.0f, 0.0f, 1.0f) * (1.0f - daylight);
    if (daylight > 0.75f) {
        skyColor0 = day0;
        skyColor1 = day1;
        skyColor2 = day2;
    } else if (daylight > 0.6f) {
        float t = (daylight - 0.6f) / 0.15f;
        skyColor0 = day0 * t + sunset0 * (1.0f - t);
        skyColor1 = day1 * t + sunset1 * (1.0f - t);
        skyColor2 = day2 * t + sunset2 * (1.0f - t);
    } else if (daylight > 0.4f) {
        float t = (daylight - 0.4f) / 0.2f;
        skyColor0 = sunset0 * t + dusk0 * (1.0f - t);
        skyColor1 = sunset1 * t + dusk1 * (1.0f - t);
        skyColor2 = sunset2 * t + dusk2 * (1.0f - t);
    } else {
        float t = daylight / 0.4f;
        skyColor0 = night0 * (1.0f - t) + dusk0 * t;
        skyColor1 = night1 * (1.0f - t) + dusk1 * t;
        skyColor2 = night2 * (1.0f - t) + dusk2 * t;
    }
    if (underwater) {
        plasma::Vector<4, float> water(daylight * 0.1, daylight * 0.15, daylight, 1.0f);
        skyColor0 = water;
        skyColor1 = water;
    }

    plasma::Vector<4, float> dayFront(1.1f, 1.1f, 0.9f, 0.0f);
    plasma::Vector<4, float> dayBack(0.5f, 0.6f, 0.8f, 1.0f);
    plasma::Vector<4, float> duskFront(1.0f, 0.9f, 0.6f, 1.0f);
    plasma::Vector<4, float> duskBack(0.6f, 0.5f, 1.0f);      // fourth component unset
    plasma::Vector<4, float> nightFront(0.01f, 0.02f, 0.01f); // fourth component unset
    plasma::Vector<4, float> nightBack(0.0f, 0.02f, 0.03f);   // fourth component unset
    if (fire > 0.0f) {
        dayFront = dayFront * (1.0f - fire) + plasma::Vector<4, float>(1.0f, 0.8f, 0.1f, 0.0f) * fire;
        duskFront = duskFront * (1.0f - fire) + plasma::Vector<4, float>(0.8f, 0.8f, 0.0f, 0.0f) * fire;
        nightFront = nightFront * (1.0f - fire) + plasma::Vector<4, float>(1.0f, 0.8f, 0.1f, 0.0f) * fire;
        dayBack = dayBack * (1.0f - fire) + plasma::Vector<4, float>(1.0f, 0.5f, 0.5f, 0.0f) * fire;
        duskBack = duskBack * (1.0f - fire) + plasma::Vector<4, float>(0.8f, 0.4f, 0.4f, 0.0f) * fire;
        nightBack = nightBack * (1.0f - fire) + plasma::Vector<4, float>(0.7f, 0.3f, 0.3f, 0.0f) * fire;
    }
    if (daylight > 0.5f) {
        float t = (daylight - 0.5f) * 2.0f;
        frontLight = dayFront * t + duskFront * (1.0f - t);
        backLight = dayBack * t + duskBack * (1.0f - t);
    } else {
        float t = daylight * 2.0f;
        frontLight = nightFront * (1.0f - t) + duskFront * t;
        backLight = nightBack * (1.0f - t) + duskBack * t;
    }
}
#endif

// The house of the block's zone with a room cell (kind 1) whose box holds the block: cells are 13
// x 13 x 7 blocks from the house's position, and the box includes its far faces.
House *World::getHouse(int x, int y, int z)
{
    Zone *zone = this->zone(x / BlocksPerZone, y / BlocksPerZone);
    if (!zone || zone->m_houses.empty())
        return 0;
    for (std::vector<House *>::iterator house = zone->m_houses.begin(); house != zone->m_houses.end(); ++house) {
        for (int i = 0; i < (*house)->sizeX(); i++) {
            for (int j = 0; j < (*house)->sizeY(); j++) {
                for (int k = 0; k < (*house)->sizeZ(); k++) {
                    if ((*house)->cell(i, j, k).kind != HouseCellFloor)
                        continue;
                    plasma::Vector<3, int> origin((*house)->m_position[0] + i * 13, (*house)->m_position[1] + j * 13,
                                                  (*house)->m_position[2] + k * 7);
                    if (x >= origin[0] && y >= origin[1] && z >= origin[2] && x <= origin[0] + 13 &&
                        y <= origin[1] + 13 && z <= origin[2] + 6)
                        return *house;
                }
            }
        }
    }
    return 0;
}

bool World::inRange(const plasma::Vector<3, Fixed> &a, const plasma::Vector<3, Fixed> &b, bool objects, float range)
{
    plasma::Vector<3, float> direction = plasma::Vector<3, float>(b - a);
    float squaredLength = direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2];
    if (squaredLength < 0.0001f)
        return true;
    if (squaredLength > range * range)
        return false;
    float length = sqrt(squaredLength);
    direction /= length;
    return rayLength(a, direction, length, false, objects) >= length - 0.01f;
}

// Whether a box of the size centred at the position collides: a solid block on the box's surface
// (the blocks inside it are not tested) or, with testObjects, a zone object listed in the 3 x 3
// cells of 8 x 8 blocks around it. Objects of types 6, 7 and 9 never block, those of types 1, 2,
// 3, 5 and 8 only while active; an odd rotation swaps an object's width and depth, and its
// position is the bottom of its box.
bool World::isBlocked(const plasma::Vector<3, Fixed> &position, const plasma::Vector<3, float> &size, bool testObjects)
{
    plasma::Vector<3, int> first(position - plasma::Vector<3, Fixed>(size * 0.5f));
    plasma::Vector<3, int> last(position + plasma::Vector<3, Fixed>(size * 0.5f));
    int x = first[0];
    while (x <= last[0]) {
        int y = first[1];
        while (y <= last[1]) {
            int z = first[2];
            while (z <= last[2]) {
                bool inside = x > first[0] && x < last[0] && y > first[1] && y < last[1] && z > first[2] && z < last[2];
                if (!inside && getBlock(x, y, z, 0).isSolid())
                    return true;
                z++;
            }
            y++;
        }
        x++;
    }
    if (!testObjects)
        return false;
    int cellX = (int)position[0] / 8;
    int cellY = (int)position[1] / 8;
    for (int x = cellX - 1; x <= cellX + 1; x++) {
        for (int y = cellY - 1; y <= cellY + 1; y++) {
            std::list<Zone::CellRecord> *objects = getCellObjects(x, y);
            if (!objects)
                continue;
            for (std::list<Zone::CellRecord>::iterator it = objects->begin(); it != objects->end(); ++it) {
                Zone::Object *object = it->object;
                if (object->type == StaticObjectSpikeTrap || object->type == StaticObjectFireTrap ||
                    object->type == StaticObjectLever)
                    continue;
                if ((object->type == StaticObjectDoor || object->type == StaticObjectStompTrap ||
                     object->type == StaticObjectBigDoor || object->type == StaticObjectWindow ||
                     object->type == StaticObjectGate) &&
                    !object->active)
                    continue;
                plasma::Vector<3, float> objectSize = object->size;
                float width, depth;
                if (object->rotation % 2 != 0) {
                    width = objectSize[1];
                    depth = objectSize[0];
                } else {
                    width = objectSize[0];
                    depth = objectSize[1];
                }
                if (position[0] + Fixed(size[0] * 0.5f) < object->position[0] - Fixed(width * 0.5f) ||
                    position[0] - Fixed(size[0] * 0.5f) >= object->position[0] + Fixed(width * 0.5f))
                    continue;
                if (position[1] + Fixed(size[1] * 0.5f) < object->position[1] - Fixed(depth * 0.5f) ||
                    position[1] - Fixed(size[1] * 0.5f) >= object->position[1] + Fixed(depth * 0.5f))
                    continue;
                if (position[2] + Fixed(size[2] * 0.5f) < object->position[2])
                    continue;
                // Half the object's height, not the box's, below the position.
                if (position[2] - Fixed(objectSize[2] * 0.5f) < object->position[2] + Fixed(objectSize[2]))
                    return true;
            }
        }
    }
    return false;
}

// Whether a solid block lies under a box of the size centred at the position: the blocks of the
// layer 0.2 blocks below its bottom. followPath uses this bottom-layer test to choose supported
// waypoints.
bool World::isOnGround(const plasma::Vector<3, Fixed> &position, const plasma::Vector<3, float> &size)
{
    plasma::Vector<3, Fixed> below(plasma::Vector<3, float>(0.0f, 0.0f, 0.2f));
    plasma::Vector<3, int> low = toBlock(position - plasma::Vector<3, Fixed>(size * 0.5f) - below);
    plasma::Vector<3, int> high = toBlock(position + plasma::Vector<3, Fixed>(size * 0.5f));
    for (int x = low[0]; x <= high[0]; x++) {
        for (int y = low[1]; y <= high[1]; y++) {
            if (getBlock(x, y, low[2], 0).isSolid())
                return true;
        }
    }
    return false;
}

Zone::ZoneItem *World::item(const plasma::Vector<3, int> &id)
{
    Zone *zone = this->zone(id[0], id[1]);
    if (zone) {
        int index = id[2];
        if (index >= 0 && index < (int)zone->m_items.size())
            return &zone->m_items[index];
    }
    return 0;
}

// The dictionary key of an item type and subtype, or L"" (the release keeps these in Speech).
std::wstring World::itemKey(int type, int subtype)
{
    std::map<std::pair<int, int>, std::wstring>::iterator it = m_itemKeys.find(std::make_pair(type, subtype));
    if (it == m_itemKeys.end())
        return L"";
    return it->second;
}

// A point near (x, y) / 16384, displaced by two octaves of noise per coordinate from the seed's
// offsets m_coordinateWarpOffsets; generateZone and roadDistance use the point.
plasma::Vector<2, double> World::jitter(int x, int y)
{
    // Coordinate warp: fine noise weighted by 0.1 plus broad noise, with 500-block amplitude before
    // region normalization.
    plasma::Vector<2, double> result;
    result[0] = (noise(x * 0.01 + m_coordinateWarpOffsets[2], y * 0.01 + m_coordinateWarpOffsets[3]) * 0.1 +
                 noise(x * 0.0005 + m_coordinateWarpOffsets[0], y * 0.0005 + m_coordinateWarpOffsets[1])) *
                    500.0 / 16384 +
                x / 16384.0;
    result[1] = (noise(x * 0.01 + m_coordinateWarpOffsets[6], y * 0.01 + m_coordinateWarpOffsets[7]) * 0.1 +
                 noise(x * 0.0005 + m_coordinateWarpOffsets[4], y * 0.0005 + m_coordinateWarpOffsets[5])) *
                    500.0 / 16384 +
                y / 16384.0;
    return result;
}

// The dictionary key of the structure a region cell holds.
std::wstring World::cellKey(Region::Cell *cell)
{
    return structureKey(cell->kind, cell->style);
}

namespace {
const int MinimumAmbientBlockLight = 5;
}

float World::lightLevel(const plasma::Vector<3, Fixed> &position)
{
    const Block &block = getBlock((int)position[0], (int)position[1], (int)position[2], 0);
    int type = block.type & ~BlockFlagMask;
    int light;
    if (type == 13)
        light = 255;
    else if (type == BlockAir || type == BlockWater)
        light = block.color[0] < MinimumAmbientBlockLight ? MinimumAmbientBlockLight : block.color[0];
    else
        light = 0;
    return light / 255.0f;
}

// The dictionary key of an item material; L"Material" for the others.
std::wstring World::materialKey(int material)
{
    switch (material) {
    case ItemMaterialIron:
        return L"MaterialIron";
    case ItemMaterialWood:
        return L"MaterialWood";
    case ItemMaterialGold:
        return L"MaterialGold";
    case ItemMaterialIce:
    case ItemMaterialIceSpirit:
        return L"MaterialIce";
    case ItemMaterialLight:
        return L"MaterialLight";
    case ItemMaterialGlass:
        return L"MaterialGlass";
    case ItemMaterialSilk:
        return L"MaterialSilk";
    case ItemMaterialLinen:
        return L"MaterialLinen";
    case ItemMaterialCotton:
        return L"MaterialCotton";
    case ItemMaterialSilver:
        return L"MaterialSilver";
    case ItemMaterialCopper:
        return L"MaterialCopper";
    case ItemMaterialSandstone:
        return L"MaterialSandstone";
    case ItemMaterialEmerald:
        return L"MaterialEmerald";
    case ItemMaterialSapphire:
        return L"MaterialSapphire";
    case ItemMaterialRuby:
        return L"MaterialRuby";
    case ItemMaterialDiamond:
        return L"MaterialDiamond";
    case ItemMaterialObsidian:
        return L"MaterialObsidian";
    case ItemMaterialSaurian:
        return L"MaterialSaurian";
    case ItemMaterialParrot:
        return L"MaterialParrot";
    case ItemMaterialMammoth:
        return L"MaterialMammoth";
    case ItemMaterialPlant:
        return L"MaterialPlant";
    case ItemMaterialBone:
        return L"MaterialBone";
    case ItemMaterialFireSpirit:
        return L"MaterialFire";
    case ItemMaterialUnholySpirit:
        return L"MaterialUnholy";
    case ItemMaterialWindSpirit:
        return L"MaterialWind";
    default:
        return L"Material";
    }
}

#ifndef CUBE_SERVER
// Where a dead player revives: at a statue of the 3 x 3 zones around the position, the one nearest
// across the ground after the very nearest; at the only statue there is; or at the position itself
// without one.
plasma::Vector<3, Fixed> World::respawnPosition(const plasma::Vector<3, Fixed> &position)
{
    int zoneX = (int)position[0] / BlocksPerZone;
    int zoneY = (int)position[1] / BlocksPerZone;
    std::list<plasma::Vector<3, Fixed>> statues;
    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            Zone *zone = this->zone(zoneX + dx, zoneY + dy);
            if (!zone)
                continue;
            for (std::vector<Zone::Object>::iterator object = zone->m_objects.begin(); object != zone->m_objects.end();
                 ++object) {
                if (object->type == StaticObjectStatue)
                    statues.push_back(object->position);
            }
        }
    }
    if (statues.empty())
        return position;
    if (statues.size() == 1)
        return statues.front();

    float best = -1.0f;
    std::list<plasma::Vector<3, Fixed>>::iterator nearest = statues.end();
    for (std::list<plasma::Vector<3, Fixed>>::iterator statue = statues.begin(); statue != statues.end(); ++statue) {
        plasma::Vector<3, Fixed> offset = *statue - position;
        float distance = plasma::Vector<2, float>(offset[0], offset[1]).squaredLength();
        if (best < 0.0f || distance < best) {
            best = distance;
            nearest = statue;
        }
    }
    if (nearest != statues.end())
        statues.erase(nearest);
    best = -1.0f;
    for (std::list<plasma::Vector<3, Fixed>>::iterator statue = statues.begin(); statue != statues.end(); ++statue) {
        plasma::Vector<3, Fixed> offset = *statue - position;
        float distance = plasma::Vector<2, float>(offset[0], offset[1]).squaredLength();
        if (best < 0.0f || distance < best) {
            best = distance;
            nearest = statue;
        }
    }
    return *nearest;
}
#endif

Zone::Object *World::object(const plasma::Vector<3, int> &id)
{
    Zone *zone = this->zone(id[0], id[1]);
    if (zone) {
        int index = id[2];
        if (index >= 0 && index < (int)zone->m_objects.size())
            return &zone->m_objects[index];
    }
    return 0;
}

// The target was attacked: queue its aggro towards the attacker, and alert every creature within 8
// blocks of the target that may attack the attacker and has no aggro towards it yet (aggro 0.5),
// 's onAttack.
void World::onAttack(Creature *attacker, Creature *target, ServerUpdates *updates)
{
    AggroPacket packet;
    packet.targetId = attacker->m_id;
    packet.creatureId = target->m_id;
    packet.aggro = target->m_aggro[attacker->m_id];
    updates->aggro.push_back(packet);
    for (std::map<long long, Creature *>::iterator it = m_creatures.begin(); it != m_creatures.end(); ++it) {
        Creature *&creature = it->second;
        if (creature && creature != attacker && creature != target && areEnemies(creature, attacker)) {
            plasma::Vector<3, float> distance(creature->m_entityData.position - target->m_entityData.position);
            if (distance.squaredLength() < 64.0f && creature->m_aggro[attacker->m_id] == 0.0f) {
                creature->m_aggro[attacker->m_id] = 0.5f;
                AggroPacket alert;
                alert.targetId = attacker->m_id;
                alert.creatureId = creature->m_id;
                alert.aggro = creature->m_aggro[attacker->m_id];
                updates->aggro.push_back(alert);
            }
        }
    }
}

// Settles a kill. When the local player was killed, its companion dies as well; the local player
// gains the xp for a hostile creature (hostility type 1); the killer's living companion gains it
// too, and the killer's pet item records the companion's xp and level. Nothing of the companion
// changes while the world saves nothing; GameController dispatches KillPackets here.
void World::onKill(const KillPacket &kill)
{
    std::map<long long, Creature *>::iterator killerIt = m_creatures.find(kill.killerId);
    Creature *killer = killerIt == m_creatures.end() ? 0 : killerIt->second;
    std::map<long long, Creature *>::iterator targetIt = m_creatures.find(kill.targetId);
    Creature *target = targetIt == m_creatures.end() ? 0 : targetIt->second;
    if (!killer || !target)
        return;
    if (target == m_localPlayer) {
        if (creatureByID(m_localPlayer->m_companionId)) {
            creatureByID(m_localPlayer->m_companionId)->m_entityData.hp = 0;
            creatureByID(m_localPlayer->m_companionId)->m_aggro.clear();
        }
        m_localPlayer->m_opponentId = 0;
    }
    if (m_localPlayer && m_localPlayer == killer && target->m_entityData.hostilityType == HostilityEnemy) {
        m_localPlayer->m_entityData.xp += kill.xp;
        m_localPlayer->applyLevelUps(this);
    }
    if (m_client)
        return;
    Creature *companion = creatureByID(killer->m_companionId);
    if (companion && companion->m_entityData.hp > 0) {
        companion->m_entityData.xp += kill.xp;
        companion->applyLevelUps(this);
        killer->m_entityData.equipment.pet.modifier = companion->m_entityData.xp;
        killer->m_entityData.equipment.pet.level = companion->m_entityData.level;
        killer->m_lastPetItem = killer->m_entityData.equipment.pet;
    }
}

// A two-syllable proper name: the first syllable from (seed * 13 + seed / 7), the second from
// (seed * 7 + seed / 10), from tables of ten by race and sex for the playable races (and the
// human-like ones), else from the default tables of twenty. randomName, with fewer syllables and a
// simpler hash.
std::wstring World::randomName(unsigned int seed, int race)
// Proper-name syllables: paired first/second arrays by race and sex.
{
    static std::wstring defaultFirst[20] = {L"Kro",  L"Aru",  L"As",  L"Ge",   L"Kur", L"Lugo", L"Iko",
                                            L"Liku", L"Tero", L"Var", L"An",   L"The", L"Ga",   L"Lan",
                                            L"Dura", L"Dama", L"Se",  L"Thal", L"Nar", L"San"};
    static std::wstring defaultSecond[20] = {L"no",  L"mi",  L"la",  L"sel",  L"ron", L"ion", L"kor",
                                             L"lon", L"rok", L"tar", L"den",  L"gar", L"dar", L"dara",
                                             L"lan", L"ka",  L"gor", L"rior", L"ria", L"mor"};
    static std::wstring humanMaleFirst[10] = {L"De",  L"Ro",   L"As",  L"Ge", L"An",
                                              L"Pat", L"Wolf", L"Ale", L"Mi", L"Ben"};
    static std::wstring humanMaleSecond[10] = {L"rek", L"man",    L"gram", L"rald", L"rick",
                                               L"ram", L"sander", L"kal",  L"ton",  L"ny"};
    static std::wstring humanFemaleFirst[10] = {L"Sa",   L"Jen",  L"Kla", L"Mi", L"La",
                                                L"Auri", L"Mela", L"Alu", L"Ve", L"Est"};
    static std::wstring humanFemaleSecond[10] = {L"rah", L"ny", L"ria",   L"elle", L"na",
                                                 L"ga",  L"ma", L"riana", L"rona", L"una"};
    static std::wstring elfMaleFirst[10] = {L"El", L"Li", L"Dri", L"Elan", L"Le", L"Ly", L"A", L"Tho", L"Da", L"Les"};
    static std::wstring elfMaleSecond[10] = {L"ric",    L"kun",  L"zic",  L"dor",    L"las",
                                             L"sander", L"reon", L"reas", L"lundra", L"andor"};
    static std::wstring elfFemaleFirst[10] = {L"Ar", L"A", L"Si", L"Mer", L"Lu", L"Ari", L"Su", L"Sira", L"Hia", L"Li"};
    static std::wstring elfFemaleSecond[10] = {L"weya", L"luna",  L"laya", L"leya", L"ma",
                                               L"lia",  L"matra", L"vy",   L"zyna", L"ly"};
    static std::wstring goblinMaleFirst[10] = {L"Gra", L"Xe", L"Ur",  L"Bel",  L"Chu",
                                               L"Ki",  L"Go", L"Zim", L"Kubo", L"Raz"};
    static std::wstring goblinMaleSecond[10] = {L"zic", L"nax", L"tik", L"tuk", L"ruk",
                                                L"bo",  L"rix", L"mi",  L"tok", L"nor"};
    static std::wstring goblinFemaleFirst[10] = {L"Kur", L"Xe", L"Ki",  L"Am", L"Zifa",
                                                 L"Zu",  L"Ma", L"Chi", L"Zi", L"Mia"};
    static std::wstring goblinFemaleSecond[10] = {L"ra", L"lia",  L"bi", L"ba", L"ly",
                                                  L"ki", L"bara", L"mi", L"zy", L"xa"};
    static std::wstring lizardmanMaleFirst[10] = {L"Cho",  L"Zer", L"Kaz", L"Kraz", L"Zel",
                                                  L"Drak", L"Lo",  L"Raz", L"Spi",  L"Zen"};
    static std::wstring lizardmanMaleSecond[10] = {L"rux", L"rek", L"zic", L"tac", L"mec",
                                                   L"kor", L"rax", L"zor", L"ro",  L"go"};
    static std::wstring lizardmanFemaleFirst[10] = {L"Zi",   L"Zer", L"Az",  L"Ki",  L"Iza",
                                                    L"Drak", L"Li",  L"May", L"Spi", L"Zan"};
    static std::wstring lizardmanFemaleSecond[10] = {L"rya", L"ri",   L"zia", L"ki", L"mah",
                                                     L"ira", L"maya", L"ana", L"ra", L"ya"};
    static std::wstring dwarfMaleFirst[10] = {L"Tor", L"Gem",  L"Bar", L"Me",   L"As",
                                              L"Tem", L"Arak", L"Ur",  L"Grim", L"Xor"};
    static std::wstring dwarfMaleSecond[10] = {L"lox", L"bart", L"kor", L"thos", L"kun",
                                               L"bur", L"thor", L"lok", L"li",   L"bor"};
    static std::wstring dwarfFemaleFirst[10] = {L"Mum", L"Grun", L"Brun", L"Hei",  L"Mo",
                                                L"Hil", L"Ur",   L"Lum",  L"Grim", L"Xor"};
    static std::wstring dwarfFemaleSecond[10] = {L"pie",  L"hild", L"di",  L"muna", L"ki",
                                                 L"trud", L"sa",   L"axa", L"ira",  L"ika"};
    static std::wstring orcMaleFirst[10] = {L"Uz", L"Ur", L"Chu", L"Ku", L"Mor", L"Ura", L"Ak", L"Ur", L"Or", L"Gor"};
    static std::wstring orcMaleSecond[10] = {L"ku",  L"ruk",   L"muk",  L"thak", L"dok",
                                             L"tor", L"rorok", L"chak", L"kaz",  L"rack"};
    static std::wstring orcFemaleFirst[10] = {L"Uz", L"Ur", L"Chu", L"Ku", L"Mor", L"Ura", L"Ak", L"Ur", L"Or", L"Gor"};
    static std::wstring orcFemaleSecond[10] = {L"ka",  L"rua",   L"mua",   L"thara", L"daka",
                                               L"tah", L"rorah", L"chaka", L"kaya",  L"rana"};
    static std::wstring undeadMaleFirst[10] = {L"Chu", L"Dro", L"Al",  L"Dem", L"Mor",
                                               L"Mur", L"Cro", L"Zul", L"Dra", L"The"};
    static std::wstring undeadMaleSecond[10] = {L"lu",  L"kor",   L"card", L"morius", L"enius",
                                                L"tus", L"demar", L"lus",  L"ruul",   L"zad"};
    static std::wstring undeadFemaleFirst[10] = {L"Xu",  L"Dra", L"Al",  L"Dem", L"Mor",
                                                 L"Myr", L"Cra", L"Zul", L"Dri", L"Thu"};
    static std::wstring undeadFemaleSecond[10] = {L"la",   L"kira",  L"cara", L"moria", L"ena",
                                                  L"tana", L"diria", L"laza", L"rah",   L"zazah"};
    static std::wstring frogmanMaleFirst[10] = {L"Qua", L"Rib", L"Quib", L"Zib", L"Il",
                                                L"Wok", L"Wib", L"Moko", L"Sli", L"Bul"};
    static std::wstring frogmanMaleSecond[10] = {L"rik", L"bit", L"ble", L"bik", L"wak",
                                                 L"wok", L"wib", L"luk", L"mey", L"bak"};
    static std::wstring frogmanFemaleFirst[10] = {L"Qua", L"Rib", L"Quib", L"Zib", L"Il",
                                                  L"Wok", L"Wib", L"Moko", L"Sli", L"Bul"};
    static std::wstring frogmanFemaleSecond[10] = {L"ria",  L"bia",   L"bla", L"bia",  L"waka",
                                                   L"woka", L"wibba", L"lua", L"maya", L"ba"};
    std::wstring name; // unused
    switch (race) {
    case RaceElfMale:
        return elfMaleFirst[(seed * 13 + seed / 7) % 10] + elfMaleSecond[(seed * 7 + seed / 10) % 10];
    case RaceElfFemale:
        return elfFemaleFirst[(seed * 13 + seed / 7) % 10] + elfFemaleSecond[(seed * 7 + seed / 10) % 10];
    case RaceHumanMale:
    case RaceOldMan:
    case RaceWizard:
    case RaceNomadMale:
        return humanMaleFirst[(seed * 13 + seed / 7) % 10] + humanMaleSecond[(seed * 7 + seed / 10) % 10];
    case RaceHumanFemale:
    case RaceWitch:
    case RaceNomadFemale:
        return humanFemaleFirst[(seed * 13 + seed / 7) % 10] + humanFemaleSecond[(seed * 7 + seed / 10) % 10];
    case RaceGoblinMale:
        return goblinMaleFirst[(seed * 13 + seed / 7) % 10] + goblinMaleSecond[(seed * 7 + seed / 10) % 10];
    case RaceGoblinFemale:
        return goblinFemaleFirst[(seed * 13 + seed / 7) % 10] + goblinFemaleSecond[(seed * 7 + seed / 10) % 10];
    case RaceLizardmanMale:
        return lizardmanMaleFirst[(seed * 13 + seed / 7) % 10] + lizardmanMaleSecond[(seed * 7 + seed / 10) % 10];
    case RaceLizardmanFemale:
        return lizardmanFemaleFirst[(seed * 13 + seed / 7) % 10] + lizardmanFemaleSecond[(seed * 7 + seed / 10) % 10];
    case RaceDwarfMale:
        return dwarfMaleFirst[(seed * 13 + seed / 7) % 10] + dwarfMaleSecond[(seed * 7 + seed / 10) % 10];
    case RaceDwarfFemale:
        return dwarfFemaleFirst[(seed * 13 + seed / 7) % 10] + dwarfFemaleSecond[(seed * 7 + seed / 10) % 10];
    case RaceOrcMale:
        return orcMaleFirst[(seed * 13 + seed / 7) % 10] + orcMaleSecond[(seed * 7 + seed / 10) % 10];
    case RaceOrcFemale:
        return orcFemaleFirst[(seed * 13 + seed / 7) % 10] + orcFemaleSecond[(seed * 7 + seed / 10) % 10];
    case RaceFrogmanMale:
        return frogmanMaleFirst[(seed * 13 + seed / 7) % 10] + frogmanMaleSecond[(seed * 7 + seed / 10) % 10];
    case RaceFrogmanFemale:
        return frogmanFemaleFirst[(seed * 13 + seed / 7) % 10] + frogmanFemaleSecond[(seed * 7 + seed / 10) % 10];
    case RaceUndeadMale:
        return undeadMaleFirst[(seed * 13 + seed / 7) % 10] + undeadMaleSecond[(seed * 7 + seed / 10) % 10];
    case RaceUndeadFemale:
        return undeadFemaleFirst[(seed * 13 + seed / 7) % 10] + undeadFemaleSecond[(seed * 7 + seed / 10) % 10];
    default:
        return defaultFirst[(seed * 13 + seed / 7) % 20] + defaultSecond[(seed * 7 + seed / 10) % 20];
    }
}

float World::rayLength(const plasma::Vector<3, Fixed> &position, const plasma::Vector<3, float> &direction,
                       float length, bool solid, bool objects)
{
    // Block by block: each step goes to the nearest block face the ray crosses (at most 500 steps).
    float travelled = 0.0f;
    plasma::Vector<3, int> block(position);
    if (position[0].raw < 0)
        block[0]--;
    if (position[1].raw < 0)
        block[1]--;
    if (position[2].raw < 0)
        block[2]--;
    int count = 0;
    while (length > travelled) {
        if (getBlock(block[0], block[1], block[2], 0).isSolid() != solid)
            break;
        if (count > 500)
            break;
        count++;
        plasma::Vector<3, Fixed> point = position + plasma::Vector<3, Fixed>(direction * travelled);
        float step = 10.0f;
        int axis = 0;
        for (int i = 0; i < 3; i++) {
            if (direction[i] * direction[i] < 0.000001f)
                continue;
            float distance;
            if (direction[i] > 0.0f)
                distance = (float)((Fixed(block[i] + 1) - point[i]) / direction[i]);
            else
                distance = (float)((Fixed(block[i]) - point[i]) / direction[i]);
            if (step > distance) {
                axis = i;
                step = distance;
            }
        }
        if (direction[axis] > 0.0f)
            block[axis]++;
        else
            block[axis]--;
        travelled += step;
    }
    if (travelled > length)
        travelled = length;

    if (objects) {
        // The closed doors and similar objects of the zones between the start and the end: only their
        // vertical faces stop the ray.
        plasma::Vector<2, int> low((int)position[0] / BlocksPerZone, (int)position[1] / BlocksPerZone);
        plasma::Vector<2, int> high(block[0] / BlocksPerZone, block[1] / BlocksPerZone);
        if (low[0] > high[0]) {
            int swap = low[0];
            low[0] = high[0];
            high[0] = swap;
        }
        if (low[1] > high[1]) {
            int swap = low[1];
            low[1] = high[1];
            high[1] = swap;
        }
        for (int x = low[0]; x <= high[0]; x++) {
            for (int y = low[1]; y <= high[1]; y++) {
                Zone *zone = this->zone(x, y);
                if (!zone)
                    continue;
                for (std::vector<Zone::Object>::iterator it = zone->m_objects.begin(); it != zone->m_objects.end();
                     ++it) {
                    if (!((it->type == StaticObjectCastleWindow || it->type == StaticObjectWindow ||
                           it->type == StaticObjectGate || it->type == StaticObjectStompTrap) &&
                          it->active))
                        continue;
                    plasma::Vector<3, float> size = it->size;
                    if (it->rotation % 2 != 0)
                        size = plasma::Vector<3, float>(size[1], size[0], size[2]);
                    if (direction[0] != 0.0f) {
                        Fixed halfWidth(size[0] * 0.5f);
                        float distance = (float)(it->position[0] - halfWidth - position[0]) / direction[0];
                        if (distance >= 0.0f && travelled > distance) {
                            plasma::Vector<3, Fixed> hit = position + plasma::Vector<3, Fixed>(direction * distance);
                            Fixed halfDepth(size[1] * 0.5f);
                            if (hit[1] >= it->position[1] - halfDepth && hit[2] >= it->position[2] &&
                                hit[1] < it->position[1] + halfDepth && hit[2] < it->position[2] + Fixed(size[2]))
                                travelled = distance;
                        }
                        distance = (float)(it->position[0] + halfWidth - position[0]) / direction[0];
                        if (distance >= 0.0f && travelled > distance) {
                            plasma::Vector<3, Fixed> hit = position + plasma::Vector<3, Fixed>(direction * distance);
                            Fixed halfDepth(size[1] * 0.5f);
                            if (hit[1] >= it->position[1] - halfDepth && hit[2] >= it->position[2] &&
                                hit[1] < it->position[1] + halfDepth && hit[2] < it->position[2] + Fixed(size[2]))
                                travelled = distance;
                        }
                    }
                    if (direction[1] != 0.0f) {
                        Fixed halfDepth(size[1] * 0.5f);
                        float distance = (float)(it->position[1] - halfDepth - position[1]) / direction[1];
                        if (distance >= 0.0f && travelled > distance) {
                            plasma::Vector<3, Fixed> hit = position + plasma::Vector<3, Fixed>(direction * distance);
                            Fixed halfWidth(size[0] * 0.5f);
                            if (hit[0] >= it->position[0] - halfWidth && hit[2] >= it->position[2] &&
                                hit[0] < it->position[0] + halfWidth && hit[2] < it->position[2] + Fixed(size[2]))
                                travelled = distance;
                        }
                        distance = (float)(it->position[1] + halfDepth - position[1]) / direction[1];
                        if (distance >= 0.0f && travelled > distance) {
                            plasma::Vector<3, Fixed> hit = position + plasma::Vector<3, Fixed>(direction * distance);
                            Fixed halfWidth(size[0] * 0.5f);
                            if (hit[0] >= it->position[0] - halfWidth && hit[2] >= it->position[2] &&
                                hit[0] < it->position[0] + halfWidth && hit[2] < it->position[2] + Fixed(size[2]))
                                travelled = distance;
                        }
                    }
                }
            }
        }
    }
    return travelled;
}

namespace {
const int StartingRegionCoordinate = 0x200;
}

int World::regionLevel(int x, int y)
{
    if (x == StartingRegionCoordinate && y == StartingRegionCoordinate)
        return 1;
    float dx = (float)(StartingRegionCoordinate - x);
    float dy = (float)(StartingRegionCoordinate - y);
    return 2 - (int)(sqrt(dy * dy + dx * dx) * -0.75f);
}

void World::removeRecord(int x, int y)
{
    if (x < 0 || y < 0 || x >= 0x400 || y >= 0x400)
        return;
    RegionRecord *&slot = m_regionRecords[x][y];
    RegionRecord *record = slot;
    if (!record)
        return;
    enterRegions();
    enterZones();
    slot = 0;
    leaveZones();
    leaveRegions();
    delete record;
}

void World::removeRegion(int x, int y)
{
    if (x < 0 || y < 0 || x >= 0x400 || y >= 0x400)
        return;
    Region *&slot = m_regions[x][y];
    Region *region = slot;
    if (!region)
        return;
    saveRegion(x, y);
    enterRegions();
    enterZones();
    slot = 0;
    leaveZones();
    leaveRegions();
    delete region;
}

void World::removeZone(int x, int y)
{
    Region *region = getRegion(x / ZonesPerRegion, y / ZonesPerRegion);
    if (!region)
        return;
    Zone *&slot = region->m_zones[x % ZonesPerRegion][y % ZonesPerRegion];
    Zone *zone = slot;
    if (!zone)
        return;
    enterZones();
    enterRegions();
    slot = 0;
    leaveRegions();
    leaveZones();
    if (!m_client)
        saveZone(zone);
    delete zone;
}

// Deletes every creature that is not a player (hostility types 0 and 5 stay) and whose spawn is
// still in a loaded zone, clearing the spawn's respawnTimeRemaining; update calls it when an
// innkeeper advances the day.
void World::resetCreatures()
{
    std::list<std::map<long long, Creature *>::iterator> removed;
    for (std::map<long long, Creature *>::iterator it = m_creatures.begin(); it != m_creatures.end(); ++it) {
        Creature *creature = it->second;
        if (!creature)
            continue;
        unsigned char hostility = creature->m_entityData.hostilityType;
        if (hostility == HostilityPlayer || hostility == HostilityPet)
            continue;
        const plasma::Vector<3, int> &spawn = creature->m_entityData.spawnSlot;
        Zone *zone = this->zone(spawn[0], spawn[1]);
        if (zone && spawn[2] >= 0 && spawn[2] < (int)zone->m_spawns.size()) {
            zone->m_spawns[spawn[2]]->respawnTimeRemaining = 0;
            removed.push_back(it);
            delete it->second;
        }
    }
    for (std::list<std::map<long long, Creature *>::iterator>::iterator it = removed.begin(); it != removed.end(); ++it)
        m_creatures.erase(*it);
}

// Writes the mission and the monster state of each of the region's 8 x 8 records under keys with
// their coordinates (eight per region).
void World::saveRegion(int x, int y)
{
    if (m_client || m_name.empty())
        return;
    Region *region = getRegion(x, y);
    if (!region || !region->m_populated)
        return;
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 8; j++) {
            Region::Record &record = region->m_records[i * 8 + j];
            std::stringstream missionKey;
            missionKey << "mission" << x * 64 / 8 + i << "_" << y * 64 / 8 + j;
            BytesIO missionData;
            Serializer(&missionData).saveMission(record.mission);
            m_database.write(missionKey.str(), missionData);
            std::stringstream monsterKey;
            monsterKey << "monster" << x * 64 / 8 + i << "_" << y * 64 / 8 + j;
            BytesIO monsterData;
            Serializer(&monsterData).saveMonster(record.monster);
            m_database.write(monsterKey.str(), monsterData);
        }
    }
}

void World::saveZone(Zone *zone)
{
    if (m_name.empty() || !zone)
        return;
    if (!zone->m_modified && zone->m_blockChanges.empty())
        return;
    std::stringstream key;
    key << "zone" << zone->m_position[0] << "_" << zone->m_position[1];
    BytesIO data;
    Serializer(&data).saveZone(zone);
    m_database.write(key.str(), data);
}

// The dictionary key of a settlement type, or L"" (the release keeps these in Speech).
std::wstring World::settlementKey(int type, int subtype)
{
    std::map<std::pair<int, int>, std::wstring>::iterator it = m_settlementKeys.find(std::make_pair(type, subtype));
    if (it == m_settlementKeys.end())
        return L"";
    return it->second;
}

void World::setup(int seed, const std::string &name)
{
    m_seed = seed;
    srand(seed);
    m_generationSeed = rand() % 100000;
    for (int i = 0; i < 4; i++)
        m_riverNoiseOffsets[i] = rand() % 100000;
    for (int i = 0; i < 20; i++)
        m_heightNoiseOffsets[i] = rand() % 100000;
    for (int i = 0; i < 2; i++)
        m_vegetationNoiseOffsets[i] = rand() % 100000;
    for (int i = 0; i < 2; i++)
        unused8001e4[i] = rand() % 100000;
    for (int i = 0; i < 2; i++)
        unused8001ec[i] = rand() % 100000;
    for (int i = 0; i < 2; i++)
        m_grassNoiseOffsets[i] = rand() % 100000;
    for (int i = 0; i < 8; i++)
        m_coordinateWarpOffsets[i] = rand() % 100000;
    for (int i = 0; i < 6; i++)
        m_rockColorNoiseOffsets[i] = rand() % 100000;
    for (int i = 0; i < 6; i++)
        m_pavingColorNoiseOffsets[i] = rand() % 100000;
    for (int i = 0; i < 10; i++)
        m_groundColorNoiseOffsets[i] = rand() % 100000;
    for (int i = 0; i < 4; i++)
        m_sandColorNoiseOffsets[i] = rand() % 100000;
    for (int i = 0; i < 2; i++)
        unused800284[i] = rand() % 100000;
    for (int i = 0; i < 4; i++)
        m_cliffNoiseOffsets[i] = rand() % 100000;
    for (int i = 0; i < 2; i++)
        m_coordinateNameSeeds[i] = rand() % 100000;
    for (int i = 0; i < 2; i++)
        m_treeKindNoiseOffsets[i] = rand() % 100000;

    for (int x = 0; x < 0x400; x++) {
        for (int y = 0; y < 0x400; y++) {
            if (m_regions[x][y]) {
                if (!m_client) {
                    for (int i = 0; i < 64; i++) {
                        for (int j = 0; j < 64; j++)
                            saveZone(m_regions[x][y]->m_zones[i][j]);
                    }
                }
                delete m_regions[x][y];
                m_regions[x][y] = 0;
            }
            if (m_regionRecords[x][y]) {
                delete m_regionRecords[x][y];
                m_regionRecords[x][y] = 0;
            }
        }
    }
    m_database.close();
    m_spawnPoint[0] = 8396928.0f;
    m_spawnPoint[1] = 8396928.0f;
    m_spawnPoint[2] = 0.0f;
    m_name = name;
    if (m_client || m_name.empty())
        return;
    std::stringstream path;
    path << "Save/world_" << name << ".db";
    m_database.open(path.str());
    BytesIO savedClockData;
    if (m_database.read("time", savedClockData)) {
        savedClockData.read(&m_day, sizeof(m_day));
        savedClockData.read(&m_time, sizeof(m_time));
    }
}

// Changes a zone object's active state and queues its sound at its position: gates sound in either
// direction, fire and spike traps only when activated, others use the lever sound. The object's
// timer restarts from where it was (1000 ms minus the time it had). Nothing happens when the
// object already is in that state.
void World::setObjectActive(Zone::ObjectState *object, ServerUpdates *updates, bool active)
{
    if (object->active == active)
        return;
    object->transitionTime = 1000 - object->transitionTime;
    if (object->transitionTime < 0)
        object->transitionTime = 0;
    object->active = active;
    int sound;
    switch (object->type) {
    case StaticObjectGate:
        sound = SoundGate;
        break;
    case StaticObjectFireTrap:
        if (!active)
            return;
        sound = SoundFireTrap;
        break;
    case StaticObjectSpikeTrap:
        if (!active)
            return;
        sound = SoundSpikeTrap;
        break;
    default:
        sound = SoundLever;
        break;
    }
    ServerUpdates::SoundPacket packet;
    packet.sound = sound;
    packet.pitch = 1.0f;
    packet.volume = 1.0f;
    packet.position = object->position;
    updates->sounds.push_back(packet);
}

// The dictionary key of a skill, or L"" (the release keeps these in Speech).
std::wstring World::skillKey(int id)
{
    std::map<int, std::wstring>::iterator it = m_skillKeys.find(id);
    if (it == m_skillKeys.end())
        return L"";
    return it->second;
}

// A building's block position as given (World::update passes the block under a creature).
plasma::Vector<3, int> World::buildingPosition(const plasma::Vector<3, int> &position)
{
    return position;
}

// How densely plants grow at a block column, 0..1: noise of the column (offset by the seed's
// m_grassNoiseOffsets) plus a term of the column's terrainHumidity.
float World::vegetation(int x, int y, Zone *zone)
// Vegetation density curve: clamped grass noise plus a squared humidity contribution and a nonzero
// floor.
{
    float density = std::min(
        1.0f, std::max(0.0f, noise(x * 0.008 + m_grassNoiseOffsets[0], y * 0.008 + m_grassNoiseOffsets[1]) * 1.2f));
    float humidity = terrainHumidity(x, y, zone);
    density = humidity * 0.2f * humidity + 0.02f + density;
    if (density > 1.0f)
        density = 1.0f;
    return density;
}

// Removes terrain-dependent plants whose supporting block has disappeared.
void World::removeUnsupportedPlants(int x1, int y1, int x2, int y2)
{
    x1 = std::max(0, x1);
    y1 = std::max(0, y1);
    x2 = std::max(0, x2);
    y2 = std::max(0, y2);
    int firstX = x1 / BlocksPerZone;
    int lastX = x2 / BlocksPerZone;
    int firstY = y1 / BlocksPerZone;
    int lastY = y2 / BlocksPerZone;
    for (int x = firstX; x <= lastX; x++) {
        for (int y = firstY; y <= lastY; y++) {
            Zone *zone = this->zone(x, y);
            if (!zone)
                continue;
            std::list<std::list<Zone::Plant>::iterator> removed;
            for (std::list<Zone::Plant>::iterator it = zone->m_plants.begin(); it != zone->m_plants.end(); ++it) {
                if ((it->flags & (1 << PlantFlagNeedsSupport)) &&
                    !(getBlock(it->position[0], it->position[1], it->position[2] - Fixed(1), 0).type & BlockTypeMask))
                    removed.push_back(it);
            }
            for (std::list<std::list<Zone::Plant>::iterator>::iterator it = removed.begin(); it != removed.end(); ++it)
                zone->m_plants.erase(*it);
        }
    }
}

// The id (zone x, zone y, index) of the zone object nearest to the position among the objects
// listed in the 3 x 3 cells of 8 x 8 blocks around it, or (0, 0, -1) when there is none within 4
// blocks.
plasma::Vector<3, int> World::nearestObject(const plasma::Vector<3, Fixed> &position)
{
    int cellX = (int)position[0] / 8;
    int cellY = (int)position[1] / 8;
    float nearest = -1.0f;
    plasma::Vector<3, int> found(0, 0, -1);
    for (int x = cellX - 1; x <= cellX + 1; x++) {
        for (int y = cellY - 1; y <= cellY + 1; y++) {
            std::list<Zone::CellRecord> *objects = getCellObjects(x, y);
            if (objects) {
                for (std::list<Zone::CellRecord>::iterator it = objects->begin(); it != objects->end(); ++it) {
                    float distance = (it->object->position - position).squaredLength();
                    if (found[2] < 0 || distance < nearest) {
                        found = it->id;
                        nearest = distance;
                    }
                }
            }
        }
    }
    if (nearest < 0.0f || nearest > 16.0f)
        return plasma::Vector<3, int>(0, 0, -1);
    return found;
}

plasma::Vector<3, float> World::wind(const plasma::Vector<3, Fixed> &position, const plasma::Vector<3, float> &velocity)
{
    // Noise of the position only; the velocity is not used. Wind field: independent horizontal noise
    // channels at 0.05 with amplitude 2, and an upward-biased 0.02 channel.
    plasma::Vector<3, float> wind;
    wind[0] = noise(toBlocks(scaled(position[0], 0.05)), toBlocks(scaled(position[1], 0.05))) * 2.0f;
    wind[1] =
        noise(toBlocks(Fixed(3455) - scaled(position[0], -0.05)), toBlocks(Fixed(8438) - scaled(position[1], -0.05))) *
        2.0f;
    wind[2] =
        noise(toBlocks(Fixed(4234) - scaled(position[0], -0.02)), toBlocks(Fixed(5345) - scaled(position[1], -0.02))) +
        0.5f;
    return wind;
}

// The dictionary key of a town district, or L"" (the release keeps these in Speech).
std::wstring World::structureKey(int type, int subtype)
{
    std::map<std::pair<int, int>, std::wstring>::iterator it = m_structureKeys.find(std::make_pair(type, subtype));
    if (it == m_structureKeys.end())
        return L"";
    return it->second;
}

// A two-syllable place name for block column (x, y): the syllables are picked by the jittered
// region cell and the world's seed offsets m_coordinateNameSeeds. Empty outside the world.
namespace {
const int CoordinateNameHashMultiplier = 3;
}

std::wstring World::generateCoordinateName(int x, int y)
{
    if (x < 0 || x >= BlocksPerWorld || y >= BlocksPerWorld)
        // Coordinate-name prefix and suffix syllable tables.
        return L"";
    static const std::wstring prefixes[20] = {L"Hy",  L"Ty",  L"Tri", L"Ge", L"Go", L"Lu",  L"Der",
                                              L"Ter", L"Sel", L"Ko",  L"Am", L"Ka", L"Kel", L"Ko",
                                              L"Sa",  L"Mo",  L"Ru",  L"Al", L"Ta", L"Har"};
    static const std::wstring suffixes[20] = {L"ra",   L"mala",  L"ura", L"el",  L"al",   L"mer", L"kun",
                                              L"la",   L"mur",   L"gu",  L"la",  L"go",   L"sel", L"rion",
                                              L"remo", L"antis", L"uro", L"lic", L"reon", L"tuma"};
    plasma::Vector<2, int> cell(jitter(x, y));
    int first = cell[1] * CoordinateNameHashMultiplier + m_coordinateNameSeeds[0] + cell[0];
    int second = cell[0] * CoordinateNameHashMultiplier + m_coordinateNameSeeds[1] + cell[1];
    std::wstring name(prefixes[first % 20]);
    name += suffixes[second % 20];
    return name;
}

}
