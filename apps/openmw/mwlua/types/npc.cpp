#include "types.hpp"

#include <components/esm3/loadnpc.hpp>
#include <components/lua/luastate.hpp>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/world.hpp>
#include <apps/openmw/mwmechanics/npcstats.hpp>
#include <apps/openmw/mwworld/class.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>

#include "../stats.hpp"

namespace sol
{
    template <>
    struct is_automagical<ESM::NPC> : std::false_type
    {
    };
}

namespace MWLua
{
    void addNpcBindings(sol::table npc, const Context& context)
    {
        addNpcStatsBindings(npc, context);

        const MWWorld::Store<ESM::NPC>* store = &MWBase::Environment::get().getWorld()->getStore().get<ESM::NPC>();
        npc["record"]
            = sol::overload([](const Object& obj) -> const ESM::NPC* { return obj.ptr().get<ESM::NPC>()->mBase; },
                [store](const std::string& recordId) -> const ESM::NPC* {
                    return store->find(ESM::RefId::stringRefId(recordId));
                });
        sol::usertype<ESM::NPC> record = context.mLua->sol().new_usertype<ESM::NPC>("ESM3_NPC");
        record[sol::meta_function::to_string]
            = [](const ESM::NPC& rec) { return "ESM3_NPC[" + rec.mId.getRefIdString() + "]"; };
        record["name"] = sol::readonly_property([](const ESM::NPC& rec) -> std::string { return rec.mName; });
        record["race"]
            = sol::readonly_property([](const ESM::NPC& rec) -> std::string { return rec.mRace.getRefIdString(); });
        record["class"]
            = sol::readonly_property([](const ESM::NPC& rec) -> std::string { return rec.mClass.getRefIdString(); });
        record["mwscript"]
            = sol::readonly_property([](const ESM::NPC& rec) -> std::string { return rec.mScript.getRefIdString(); });
        record["hair"]
            = sol::readonly_property([](const ESM::NPC& rec) -> std::string { return rec.mHair.getRefIdString(); });
        record["head"]
            = sol::readonly_property([](const ESM::NPC& rec) -> std::string { return rec.mHead.getRefIdString(); });

        // This function is game-specific, in future we should replace it with something more universal.
        npc["isWerewolf"] = [](const Object& o) {
            const MWWorld::Class& cls = o.ptr().getClass();
            if (cls.isNpc())
                return cls.getNpcStats(o.ptr()).isWerewolf();
            else
                throw std::runtime_error("NPC or Player expected");
        };
    }
}
