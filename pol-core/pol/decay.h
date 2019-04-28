/** @file
 *
 * @par History
 */

#ifndef __DECAY_H
#define __DECAY_H

#include "gameclck.h"
#include "reftypes.h"
#include "uobject.h"

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/key_extractors.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

namespace Pol
{
namespace Items
{
class Item;
}
namespace Core
{
void decay_single_thread( void* );

class WorldDecay
{
public:
  WorldDecay();
  void initialize();

  void addObject( Items::Item* item, gameclock_t decaytime );
  void removeObject( Items::Item* item );
  gameclock_t getDecayTime( Items::Item* item ) const;
  gameclock_t getDecayTime( const Items::Item* item ) const;
  void decayTask();

private:
  struct DecayItem
  {
    DecayItem( gameclock_t decaytime, ItemRef itemref );
    gameclock_t time;
    ItemRef obj;
  };
  struct IndexByTime
  {
  };
  struct IndexByObject
  {
  };
  struct SerialFromDecayItem
  {
    typedef u32 result_type;
    result_type operator()( const DecayItem& i ) const;
  };
  // multiindex container unique by serial_ext and ordered by decaytime
  using DecayContainer = boost::multi_index_container<
      DecayItem, boost::multi_index::indexed_by<
                     boost::multi_index::hashed_unique<boost::multi_index::tag<IndexByObject>,
                                                       SerialFromDecayItem>,
                     boost::multi_index::ordered_non_unique<
                         boost::multi_index::tag<IndexByTime>,
                         boost::multi_index::member<DecayItem, gameclock_t, &DecayItem::time>>>>;

  DecayContainer decay_cont;
};
}  // namespace Core
}  // namespace Pol
#endif
