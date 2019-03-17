/** @file
 *
 * @par History
 */

#ifndef __DECAY_H
#define __DECAY_H

#include "polclock.h"
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
void decay_items();
void decay_single_zone();


class WorldDecay
{
public:
  WorldDecay();
  void addObject( Items::Item* item, poltime_t decaytime );
  void removeObject( Items::Item* item );
  poltime_t getDecayTime( Items::Item* item ) const;
  void decayTask();

private:
  struct DecayItem
  {
    DecayItem( poltime_t decaytime, ItemRef itemref );
    poltime_t time;
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
                         boost::multi_index::member<DecayItem, poltime_t, &DecayItem::time>>>>;

  DecayContainer decay_cont;
};
}  // namespace Core
}  // namespace Pol
#endif
