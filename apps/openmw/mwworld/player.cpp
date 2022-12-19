#include "player.hpp"

#include <stdexcept>

#include <components/debug/debuglog.hpp>

#include <components/esm/defs.hpp>
#include <components/esm3/esmreader.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/loadbsgn.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/player.hpp>
#include <components/fallback/fallback.hpp>

#include "../mwworld/esmstore.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/magiceffects.hpp"
#include "../mwworld/worldmodel.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwmechanics/movement.hpp"
#include "../mwmechanics/npcstats.hpp"
#include "../mwmechanics/spellutil.hpp"

#include "../mwrender/camera.hpp"
#include "../mwrender/renderingmanager.hpp"

#include "cellstore.hpp"
#include "class.hpp"
#include "ptr.hpp"

namespace MWWorld
{
    Player::Player(const ESM::NPC* player)
        : mCellStore(nullptr)
        , mLastKnownExteriorPosition(0, 0, 0)
        , mMarkedPosition(ESM::Position())
        , mMarkedCell(nullptr)
        , mAutoMove(false)
        , mForwardBackward(0)
        , mTeleported(false)
        , mCurrentCrimeId(-1)
        , mPaidCrimeId(-1)
        , mJumping(false)
    {
        ESM::CellRef cellRef;
        cellRef.blank();
        cellRef.mRefID = "player";
        mPlayer = LiveCellRef<ESM::NPC>(cellRef, player);

        ESM::Position playerPos = mPlayer.mData.getPosition();
        playerPos.pos[0] = playerPos.pos[1] = playerPos.pos[2] = 0;
        mPlayer.mData.setPosition(playerPos);
    }

    void Player::saveStats()
    {
        MWMechanics::NpcStats& stats = getPlayer().getClass().getNpcStats(getPlayer());

        for (int i = 0; i < ESM::Skill::Length; ++i)
            mSaveSkills[i] = stats.getSkill(i).getModified();
        for (int i = 0; i < ESM::Attribute::Length; ++i)
            mSaveAttributes[i] = stats.getAttribute(i).getModified();
    }

    void Player::restoreStats()
    {
        const MWWorld::Store<ESM::GameSetting>& gmst
            = MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>();
        MWMechanics::CreatureStats& creatureStats = getPlayer().getClass().getCreatureStats(getPlayer());
        MWMechanics::NpcStats& npcStats = getPlayer().getClass().getNpcStats(getPlayer());
        MWMechanics::DynamicStat<float> health = creatureStats.getDynamic(0);
        creatureStats.setHealth(health.getBase() / gmst.find("fWereWolfHealth")->mValue.getFloat());
        for (int i = 0; i < ESM::Skill::Length; ++i)
        {
            auto& skill = npcStats.getSkill(i);
            skill.restore(skill.getDamage());
            skill.setModifier(mSaveSkills[i] - skill.getBase());
        }
        for (int i = 0; i < ESM::Attribute::Length; ++i)
        {
            auto attribute = npcStats.getAttribute(i);
            attribute.restore(attribute.getDamage());
            attribute.setModifier(mSaveAttributes[i] - attribute.getBase());
            npcStats.setAttribute(i, attribute);
        }
    }

    void Player::setWerewolfStats()
    {
        const MWWorld::Store<ESM::GameSetting>& gmst
            = MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>();
        MWMechanics::CreatureStats& creatureStats = getPlayer().getClass().getCreatureStats(getPlayer());
        MWMechanics::NpcStats& npcStats = getPlayer().getClass().getNpcStats(getPlayer());
        MWMechanics::DynamicStat<float> health = creatureStats.getDynamic(0);
        creatureStats.setHealth(health.getBase() * gmst.find("fWereWolfHealth")->mValue.getFloat());
        for (size_t i = 0; i < ESM::Attribute::Length; ++i)
        {
            // Oh, Bethesda. It's "Intelligence".
            std::string name = "fWerewolf"
                + ((i == ESM::Attribute::Intelligence) ? std::string("Intellegence")
                                                       : ESM::Attribute::sAttributeNames[i]);

            MWMechanics::AttributeValue value = npcStats.getAttribute(i);
            value.setModifier(gmst.find(name)->mValue.getFloat() - value.getModified());
            npcStats.setAttribute(i, value);
        }

        for (size_t i = 0; i < ESM::Skill::Length; i++)
        {
            // Acrobatics is set separately for some reason.
            if (i == ESM::Skill::Acrobatics)
                continue;

            // "Mercantile"! >_<
            std::string name = "fWerewolf"
                + ((i == ESM::Skill::Mercantile) ? std::string("Merchantile") : ESM::Skill::sSkillNames[i]);

            MWMechanics::SkillValue& value = npcStats.getSkill(i);
            value.setModifier(gmst.find(name)->mValue.getFloat() - value.getModified());
        }
    }

    void Player::set(const ESM::NPC* player)
    {
        mPlayer.mBase = player;
    }

    void Player::setCell(MWWorld::CellStore* cellStore)
    {
        mCellStore = cellStore;
    }

    MWWorld::Ptr Player::getPlayer()
    {
        MWWorld::Ptr ptr(&mPlayer, mCellStore);
        return ptr;
    }

    MWWorld::ConstPtr Player::getConstPlayer() const
    {
        MWWorld::ConstPtr ptr(&mPlayer, mCellStore);
        return ptr;
    }

    void Player::setBirthSign(const std::string& sign)
    {
        mSign = sign;
    }

    const std::string& Player::getBirthSign() const
    {
        return mSign;
    }

    void Player::setDrawState(MWMechanics::DrawState state)
    {
        MWWorld::Ptr ptr = getPlayer();
        ptr.getClass().getNpcStats(ptr).setDrawState(state);
    }

    bool Player::getAutoMove() const
    {
        return mAutoMove;
    }

    void Player::setAutoMove(bool enable)
    {
        MWWorld::Ptr ptr = getPlayer();

        mAutoMove = enable;

        int value = mForwardBackward;

        if (mAutoMove)
            value = 1;

        ptr.getClass().getMovementSettings(ptr).mPosition[1] = value;
    }

    void Player::setLeftRight(float value)
    {
        MWWorld::Ptr ptr = getPlayer();
        ptr.getClass().getMovementSettings(ptr).mPosition[0] = value;
    }

    void Player::setForwardBackward(float value)
    {
        MWWorld::Ptr ptr = getPlayer();

        mForwardBackward = value;

        if (mAutoMove)
            value = 1;

        ptr.getClass().getMovementSettings(ptr).mPosition[1] = value;
    }

    void Player::setUpDown(int value)
    {
        MWWorld::Ptr ptr = getPlayer();
        ptr.getClass().getMovementSettings(ptr).mPosition[2] = static_cast<float>(value);
    }

    void Player::setRunState(bool run)
    {
        MWWorld::Ptr ptr = getPlayer();
        ptr.getClass().getCreatureStats(ptr).setMovementFlag(MWMechanics::CreatureStats::Flag_Run, run);
    }

    void Player::setSneak(bool sneak)
    {
        MWWorld::Ptr ptr = getPlayer();
        ptr.getClass().getCreatureStats(ptr).setMovementFlag(MWMechanics::CreatureStats::Flag_Sneak, sneak);
    }

    void Player::yaw(float yaw)
    {
        MWWorld::Ptr ptr = getPlayer();
        ptr.getClass().getMovementSettings(ptr).mRotation[2] += yaw;
    }
    void Player::pitch(float pitch)
    {
        MWWorld::Ptr ptr = getPlayer();
        ptr.getClass().getMovementSettings(ptr).mRotation[0] += pitch;
    }
    void Player::roll(float roll)
    {
        MWWorld::Ptr ptr = getPlayer();
        ptr.getClass().getMovementSettings(ptr).mRotation[1] += roll;
    }

    MWMechanics::DrawState Player::getDrawState()
    {
        MWWorld::Ptr ptr = getPlayer();
        return ptr.getClass().getNpcStats(ptr).getDrawState();
    }

    void Player::activate()
    {
        if (MWBase::Environment::get().getWindowManager()->isGuiMode())
            return;

        MWWorld::Ptr player = getPlayer();
        const MWMechanics::NpcStats& playerStats = player.getClass().getNpcStats(player);
        bool godmode = MWBase::Environment::get().getWorld()->getGodModeState();
        if ((!godmode && playerStats.isParalyzed()) || playerStats.getKnockedDown() || playerStats.isDead())
            return;

        MWWorld::Ptr toActivate = MWBase::Environment::get().getWorld()->getFacedObject();

        if (toActivate.isEmpty())
            return;

        if (!toActivate.getClass().hasToolTip(toActivate))
            return;

        MWBase::Environment::get().getWorld()->activate(toActivate, player);
    }

    bool Player::wasTeleported() const
    {
        return mTeleported;
    }

    void Player::setTeleported(bool teleported)
    {
        mTeleported = teleported;
    }

    void Player::setAttackingOrSpell(bool attackingOrSpell)
    {
        getPlayer().getClass().getCreatureStats(getPlayer()).setAttackingOrSpell(attackingOrSpell);
    }

    void Player::setJumping(bool jumping)
    {
        mJumping = jumping;
    }

    bool Player::getJumping() const
    {
        return mJumping;
    }

    bool Player::isInCombat()
    {
        return MWBase::Environment::get().getMechanicsManager()->getActorsFighting(getPlayer()).size() != 0;
    }

    bool Player::enemiesNearby()
    {
        return MWBase::Environment::get().getMechanicsManager()->getEnemiesNearby(getPlayer()).size() != 0;
    }

    void Player::markPosition(CellStore* markedCell, const ESM::Position& markedPosition)
    {
        mMarkedCell = markedCell;
        mMarkedPosition = markedPosition;
    }

    void Player::getMarkedPosition(CellStore*& markedCell, ESM::Position& markedPosition) const
    {
        markedCell = mMarkedCell;
        if (mMarkedCell)
            markedPosition = mMarkedPosition;
    }

    void Player::clear()
    {
        mCellStore = nullptr;
        mSign.clear();
        mMarkedCell = nullptr;
        mAutoMove = false;
        mForwardBackward = 0;
        mTeleported = false;
        mJumping = false;
        mCurrentCrimeId = -1;
        mPaidCrimeId = -1;
        mPreviousItems.clear();
        mLastKnownExteriorPosition = osg::Vec3f(0, 0, 0);

        for (int i = 0; i < ESM::Skill::Length; ++i)
        {
            mSaveSkills[i] = 0.f;
        }

        for (int i = 0; i < ESM::Attribute::Length; ++i)
        {
            mSaveAttributes[i] = 0.f;
        }

        mMarkedPosition.pos[0] = 0;
        mMarkedPosition.pos[1] = 0;
        mMarkedPosition.pos[2] = 0;
        mMarkedPosition.rot[0] = 0;
        mMarkedPosition.rot[1] = 0;
        mMarkedPosition.rot[2] = 0;
    }

    void Player::write(ESM::ESMWriter& writer, Loading::Listener& progress) const
    {
        ESM::Player player;

        mPlayer.save(player.mObject);
        player.mCellId = mCellStore->getCell()->getCellId();

        player.mCurrentCrimeId = mCurrentCrimeId;
        player.mPaidCrimeId = mPaidCrimeId;

        player.mBirthsign = mSign;

        player.mLastKnownExteriorPosition[0] = mLastKnownExteriorPosition.x();
        player.mLastKnownExteriorPosition[1] = mLastKnownExteriorPosition.y();
        player.mLastKnownExteriorPosition[2] = mLastKnownExteriorPosition.z();

        if (mMarkedCell)
        {
            player.mHasMark = true;
            player.mMarkedPosition = mMarkedPosition;
            player.mMarkedCell = mMarkedCell->getCell()->getCellId();
        }
        else
            player.mHasMark = false;

        for (int i = 0; i < ESM::Attribute::Length; ++i)
            player.mSaveAttributes[i] = mSaveAttributes[i];
        for (int i = 0; i < ESM::Skill::Length; ++i)
            player.mSaveSkills[i] = mSaveSkills[i];

        player.mPreviousItems = mPreviousItems;

        writer.startRecord(ESM::REC_PLAY);
        player.save(writer);
        writer.endRecord(ESM::REC_PLAY);
    }

    bool Player::readRecord(ESM::ESMReader& reader, uint32_t type)
    {
        if (type == ESM::REC_PLAY)
        {
            ESM::Player player;
            player.load(reader);

            if (!mPlayer.checkState(player.mObject))
            {
                // this is the one object we can not silently drop.
                throw std::runtime_error("invalid player state record (object state)");
            }
            if (reader.getFormat() < 17)
                convertMagicEffects(
                    player.mObject.mCreatureStats, player.mObject.mInventory, &player.mObject.mNpcStats);
            else if (reader.getFormat() < 20)
                convertStats(player.mObject.mCreatureStats);

            if (!player.mObject.mEnabled)
            {
                Log(Debug::Warning) << "Warning: Savegame attempted to disable the player.";
                player.mObject.mEnabled = true;
            }

            mPlayer.load(player.mObject);

            for (int i = 0; i < ESM::Attribute::Length; ++i)
                mSaveAttributes[i] = player.mSaveAttributes[i];
            for (int i = 0; i < ESM::Skill::Length; ++i)
                mSaveSkills[i] = player.mSaveSkills[i];

            if (player.mObject.mNpcStats.mIsWerewolf)
            {
                if (player.mObject.mNpcStats.mWerewolfDeprecatedData)
                {
                    saveStats();
                    setWerewolfStats();
                }
                else if (reader.getFormat() < 19)
                {
                    setWerewolfStats();
                    if (player.mSetWerewolfAcrobatics)
                        MWBase::Environment::get().getMechanicsManager()->applyWerewolfAcrobatics(getPlayer());
                }
            }

            getPlayer().getClass().getCreatureStats(getPlayer()).getAiSequence().clear();

            MWBase::World& world = *MWBase::Environment::get().getWorld();

            try
            {
                mCellStore = MWBase::Environment::get().getWorldModel()->getCell(player.mCellId);
            }
            catch (...)
            {
                Log(Debug::Warning) << "Warning: Player cell '" << player.mCellId.mWorldspace << "' no longer exists";
                // Cell no longer exists. The loader will have to choose a default cell.
                mCellStore = nullptr;
            }

            if (!player.mBirthsign.empty())
            {
                const ESM::BirthSign* sign = world.getStore().get<ESM::BirthSign>().search(player.mBirthsign);
                if (!sign)
                    throw std::runtime_error("invalid player state record (birthsign does not exist)");
            }

            mCurrentCrimeId = player.mCurrentCrimeId;
            mPaidCrimeId = player.mPaidCrimeId;

            mSign = player.mBirthsign;

            mLastKnownExteriorPosition.x() = player.mLastKnownExteriorPosition[0];
            mLastKnownExteriorPosition.y() = player.mLastKnownExteriorPosition[1];
            mLastKnownExteriorPosition.z() = player.mLastKnownExteriorPosition[2];

            if (player.mHasMark && !player.mMarkedCell.mPaged)
            {
                // interior cell -> need to check if it exists (exterior cell will be
                // generated on the fly)

                if (!world.getStore().get<ESM::Cell>().search(player.mMarkedCell.mWorldspace))
                    player.mHasMark = false; // drop mark silently
            }

            if (player.mHasMark)
            {
                mMarkedPosition = player.mMarkedPosition;
                mMarkedCell = MWBase::Environment::get().getWorldModel()->getCell(player.mMarkedCell);
            }
            else
            {
                mMarkedCell = nullptr;
            }

            mForwardBackward = 0;
            mTeleported = false;

            mPreviousItems = player.mPreviousItems;

            return true;
        }

        return false;
    }

    int Player::getNewCrimeId()
    {
        return ++mCurrentCrimeId;
    }

    void Player::recordCrimeId()
    {
        mPaidCrimeId = mCurrentCrimeId;
    }

    int Player::getCrimeId() const
    {
        return mPaidCrimeId;
    }

    void Player::setPreviousItem(const std::string& boundItemId, const std::string& previousItemId)
    {
        mPreviousItems[boundItemId] = previousItemId;
    }

    std::string Player::getPreviousItem(const std::string& boundItemId)
    {
        return mPreviousItems[boundItemId];
    }

    void Player::erasePreviousItem(const std::string& boundItemId)
    {
        mPreviousItems.erase(boundItemId);
    }

    void Player::setSelectedSpell(const std::string& spellId)
    {
        Ptr player = getPlayer();
        InventoryStore& store = player.getClass().getInventoryStore(player);
        store.setSelectedEnchantItem(store.end());
        int castChance = int(MWMechanics::getSpellSuccessChance(spellId, player));
        MWBase::Environment::get().getWindowManager()->setSelectedSpell(spellId, castChance);
        MWBase::Environment::get().getWindowManager()->updateSpellWindow();
    }

    void Player::update()
    {
        auto player = getPlayer();
        const auto world = MWBase::Environment::get().getWorld();
        const auto rendering = world->getRenderingManager();
        auto& store = world->getStore();
        auto& playerClass = player.getClass();
        const auto windowMgr = MWBase::Environment::get().getWindowManager();

        if (player.getCell()->isExterior())
        {
            ESM::Position pos = player.getRefData().getPosition();
            setLastKnownExteriorPosition(pos.asVec3());
        }

        bool isWerewolf = playerClass.getNpcStats(player).isWerewolf();
        bool isFirstPerson = world->isFirstPerson();
        if (isWerewolf && isFirstPerson)
        {
            float werewolfFov = Fallback::Map::getFloat("General_Werewolf_FOV");
            if (werewolfFov != 0)
                rendering->overrideFieldOfView(werewolfFov);
            windowMgr->setWerewolfOverlay(true);
        }
        else
        {
            rendering->resetFieldOfView();
            windowMgr->setWerewolfOverlay(false);
        }

        // Sink the camera while sneaking
        bool sneaking = playerClass.getCreatureStats(player).getStance(MWMechanics::CreatureStats::Stance_Sneak);
        bool swimming = world->isSwimming(player);
        bool flying = world->isFlying(player);

        static const float i1stPersonSneakDelta
            = store.get<ESM::GameSetting>().find("i1stPersonSneakDelta")->mValue.getFloat();
        if (sneaking && !swimming && !flying)
            rendering->getCamera()->setSneakOffset(i1stPersonSneakDelta);
        else
            rendering->getCamera()->setSneakOffset(0.f);

        int blind = 0;
        const auto& magicEffects = playerClass.getCreatureStats(player).getMagicEffects();
        if (!world->getGodModeState())
            blind = static_cast<int>(magicEffects.get(ESM::MagicEffect::Blind).getModifier());
        windowMgr->setBlindness(std::clamp(blind, 0, 100));

        int nightEye = static_cast<int>(magicEffects.get(ESM::MagicEffect::NightEye).getMagnitude());
        rendering->setNightEyeFactor(std::min(1.f, (nightEye / 100.f)));
    }

}
