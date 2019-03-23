/** @file
 *
 * @par History
 * - 2005/01/23 Shinigami: decay_items & decay_thread - Tokuno MapDimension doesn't fit blocks of
 * 64x64 (WGRID_SIZE)
 * - 2010/03/28 Shinigami: Transmit Pointer as Pointer and not Int as Pointer within
 * decay_thread_shadow
 */


#include "decay.h"

#include <stddef.h>

#include "../clib/esignal.h"
#include "../clib/logfacility.h"
#include "../plib/systemstate.h"
#include "gameclck.h"
#include "globals/state.h"
#include "globals/uvars.h"
#include "item/item.h"
#include "item/itemdesc.h"
#include "objtype.h"
#include "polcfg.h"
#include "polsem.h"
#include "realms/realm.h"
#include "scrdef.h"
#include "scrsched.h"
#include "syshook.h"
#include "ufunc.h"
#include "uoscrobj.h"
#include "uworld.h"


namespace Pol
{
namespace Core
{
WorldDecay::SerialFromDecayItem::result_type WorldDecay::SerialFromDecayItem::operator()(
    const WorldDecay::DecayItem& i ) const
{
  return i.obj->serial_ext;
}

WorldDecay::DecayItem::DecayItem( poltime_t decaytime, ItemRef itemref )
    : time( decaytime ), obj( itemref )
{
}

WorldDecay::WorldDecay() : decay_cont() {}

void WorldDecay::addObject( Items::Item* item, poltime_t decaytime )
{
  // TODO onmovablechange needs to add/remove object
  // multi creation/destruction needs to add/remove objects
  // so just here as a reminder, dont like the checks here
  if ( item->orphan() || ( !item->movable() && item->objtype_ != UOBJ_CORPSE ) )
    return;
  if ( !item->itemdesc().decays_on_multis )
  {
    auto multi = item->realm->find_supporting_multi( item->x, item->y, item->z );
    if ( multi != nullptr )
      return;
  }
  auto& indexByObj = decay_cont.get<IndexByObject>();
  auto res = indexByObj.emplace( decaytime, ItemRef( item ) );
  if ( !res.second )  // emplace failed, .first is itr of "blocking" entry
    indexByObj.modify( res.first, [&decaytime]( DecayItem& i ) { i.time = decaytime; } );
  else
    item->set_decay_task( true );
}

void WorldDecay::removeObject( Items::Item* item )
{
  auto& indexByObj = decay_cont.get<IndexByObject>();
  indexByObj.erase( item->serial_ext );  // ignore error?
  item->set_decay_task( false );
}

poltime_t WorldDecay::getDecayTime( Items::Item* obj ) const
{
  if ( !obj->has_decay_task() )
    return 0;
  auto& indexByObj = decay_cont.get<IndexByObject>();
  const auto& entry = indexByObj.find( obj->serial_ext );
  if ( entry == indexByObj.cend() )
    return 0;  // TODO error
  return entry->time;
}

void WorldDecay::decayTask()
{
  auto& indexByTime = decay_cont.get<IndexByTime>();
  auto now = poltime();
  std::vector<DecayItem> decayitems;
  // need to collect possible items
  // since script calls could add/remove in container
  for ( auto& v : indexByTime )
  {
    if ( v.time > now )
      break;
    decayitems.push_back( v );
  }
  if ( decayitems.empty() )  // early out
    return;

  std::vector<Items::Item*> destroyeditems;
  std::vector<Items::Item*> delayeditems;
  for ( auto& v : decayitems )
  {
    auto item = v.obj.get();
    if ( item->orphan() )
    {
      destroyeditems.push_back( item );
      continue;
    }
    if ( item->inuse() )
    {
      delayeditems.push_back( item );
      continue;
    }
    if ( gamestate.system_hooks.can_decay )
    {
      if ( !gamestate.system_hooks.can_decay->call( item->make_ref() ) )
      {
        delayeditems.push_back( item );
        continue;
      }
    }

    const auto& descriptor = item->itemdesc();
    if ( !descriptor.destroy_script.empty() )
    {
      if ( !call_script( descriptor.destroy_script, item->make_ref() ) )
      {
        delayeditems.push_back( item );
        continue;
      }
    }
    Multi::UMulti* multi = nullptr;
    if ( descriptor.decays_on_multis )
      multi = item->realm->find_supporting_multi( item->x, item->y, item->z );

    item->spill_contents( multi );
    destroy_item( item );
    destroyeditems.push_back( item );
  }

  auto& indexByObj = decay_cont.get<IndexByObject>();
  for ( const auto& item : destroyeditems )
  {
    indexByObj.erase( item->serial_ext );
    item->set_decay_task( false );
  }
  for ( const auto& item : delayeditems )
  {
    if ( getDecayTime( item ) <= now )   // check if script has removed it or changed time
      addObject( item, now + 10 * 60 );  // delay by 10minutes like old decay system would behave
  }
}

///
/// [1] Item Decay Criteria
///     An Item is allowed to decay if ALL of the following are true:
///        - it is not In Use
///        - it is Movable, OR it is a Corpse
///        - its 'decayat' member is nonzero
///            AND the Game Clock has passed this 'decayat' time
///        - it is not supported by a multi,
///            OR its itemdesc.cfg entry specifies 'DecaysOnMultis 1'
///        - it itemdesc.cfg entry specifies no 'DestroyScript',
///            OR its 'DestroyScript' returns nonzero.
///
/// [2] Decay Action
///     Container contents are moved to the ground at the Container's location
///     before destroying the container.
///

void decay_worldzone( unsigned wx, unsigned wy, Realms::Realm* realm )
{
  Zone& zone = realm->zone[wx][wy];
  gameclock_t now = read_gameclock();
  bool statistics = Plib::systemstate.config.thread_decay_statistics;

  for ( ZoneItems::size_type idx = 0; idx < zone.items.size(); ++idx )
  {
    Items::Item* item = zone.items[idx];
    if ( statistics )
    {
      if ( item->can_decay() )
      {
        const Items::ItemDesc& descriptor = item->itemdesc();
        if ( !descriptor.decays_on_multis )
        {
          Multi::UMulti* multi = realm->find_supporting_multi( item->x, item->y, item->z );
          if ( multi == nullptr )
            stateManager.decay_statistics.temp_count_active++;
        }
        else
          stateManager.decay_statistics.temp_count_active++;
      }
    }
    if ( item->should_decay( now ) )
    {
      // check the CanDecay syshook first if it returns 1 go over to other checks
      if ( gamestate.system_hooks.can_decay )
      {
        if ( !gamestate.system_hooks.can_decay->call( new Module::EItemRefObjImp( item ) ) )
          continue;
      }

      const Items::ItemDesc& descriptor = item->itemdesc();
      Multi::UMulti* multi = realm->find_supporting_multi( item->x, item->y, item->z );

      // some things don't decay on multis:
      if ( multi != nullptr && !descriptor.decays_on_multis )
        continue;

      if ( statistics )
        stateManager.decay_statistics.temp_count_decayed++;

      if ( !descriptor.destroy_script.empty() && !item->inuse() )
      {
        bool decayok = call_script( descriptor.destroy_script, item->make_ref() );
        if ( !decayok )
          continue;
      }

      item->spill_contents( multi );
      destroy_item( item );
      --idx;
    }
  }
}


// this is used in single-thread mode only
void decay_items()
{
  static unsigned wx = ~0u;
  static unsigned wy = 0;

  Realms::Realm* realm;
  for ( auto itr = gamestate.Realms.begin(); itr != gamestate.Realms.end(); ++itr )
  {
    realm = *itr;
    if ( !--stateManager.cycles_until_decay_worldzone )
    {
      stateManager.cycles_until_decay_worldzone = stateManager.cycles_per_decay_worldzone;

      unsigned gridwidth = realm->grid_width();
      unsigned gridheight = realm->grid_height();

      if ( ++wx >= gridwidth )
      {
        wx = 0;
        if ( ++wy >= gridheight )
        {
          wy = 0;
        }
      }
      decay_worldzone( wx, wy, realm );
    }
  }
}

bool should_switch_realm( size_t index, unsigned x, unsigned y, unsigned* gridx, unsigned* gridy )
{
  (void)x;
  if ( index >= gamestate.Realms.size() )
    return true;
  Realms::Realm* realm = gamestate.Realms[index];
  if ( realm == nullptr )
    return true;

  ( *gridx ) = realm->grid_width();
  ( *gridy ) = realm->grid_height();

  // check if ++y would result in reset
  if ( y + 1 >= ( *gridy ) )
    return true;
  return false;
}

void decay_single_thread( void* arg )
{
  (void)arg;
  // calculate total grid count, based on current realms
  unsigned total_grid_count = 0;
  for ( const auto& realm : gamestate.Realms )
  {
    total_grid_count += ( realm->grid_width() * realm->grid_height() );
  }
  if ( !total_grid_count )
  {
    POLLOG_ERROR << "No realm grids?!\n";
    return;
  }
  // sweep every realm ~10minutes -> 36ms for 6 realms
  unsigned sleeptime = ( 60 * 10L * 1000 ) / total_grid_count;
  sleeptime = std::max( sleeptime, 30u );  // limit to 30ms
  bool init = true;
  size_t realm_index = ~0u;
  unsigned wx = 0;
  unsigned wy = 0;
  unsigned gridx = 0;
  unsigned gridy = 0;
  while ( !Clib::exit_signalled )
  {
    {
      PolLock lck;
      polclock_checkin();
      // check if realm_index is still valid and if y is still in valid range
      if ( should_switch_realm( realm_index, wx, wy, &gridx, &gridy ) )
      {
        ++realm_index;
        if ( realm_index >= gamestate.Realms.size() )
        {
          realm_index = 0;
          if ( !init && Plib::systemstate.config.thread_decay_statistics )
          {
            stateManager.decay_statistics.decayed.update(
                stateManager.decay_statistics.temp_count_decayed );
            stateManager.decay_statistics.active_decay.update(
                stateManager.decay_statistics.temp_count_active );
            stateManager.decay_statistics.temp_count_decayed = 0;
            stateManager.decay_statistics.temp_count_active = 0;
            POLLOG_INFO.Format(
                "DECAY STATISTICS: decayed: max {} mean {} variance {} runs {} active max {} "
                "mean "
                "{} variance {} runs {}\n" )
                << stateManager.decay_statistics.decayed.max()
                << stateManager.decay_statistics.decayed.mean()
                << stateManager.decay_statistics.decayed.variance()
                << stateManager.decay_statistics.decayed.count()
                << stateManager.decay_statistics.active_decay.max()
                << stateManager.decay_statistics.active_decay.mean()
                << stateManager.decay_statistics.active_decay.variance()
                << stateManager.decay_statistics.active_decay.count();
          }
          init = false;
        }
        wx = 0;
        wy = 0;
      }
      else
      {
        if ( ++wx >= gridx )
        {
          wx = 0;
          if ( ++wy >= gridy )
          {
            POLLOG_ERROR << "SHOULD NEVER HAPPEN\n";
            wy = 0;
          }
        }
      }
      decay_worldzone( wx, wy, gamestate.Realms[realm_index] );
      restart_all_clients();
    }
    pol_sleep_ms( sleeptime );
  }
}
}  // namespace Core
}  // namespace Pol
