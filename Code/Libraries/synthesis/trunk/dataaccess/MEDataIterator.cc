/// @file
///
/// MEDataIterator: Allow iteration across preselected data. Each 
/// iteration step is represented by the MEDataAccessor interface.
/// The idea is that an iterator object will be obtained via MEDataSource
/// which will take care of the actual method to access the data and the
/// source (a MeasurementSet or a stream). Any class controlling data selection
/// is likely to be held by a real implementation of the iterator. However,
/// it will be set up via the MEDataSource object and IS NOT a part of this
/// interface.
///
/// @copyright (c) 2007 CONRAD, All Rights Reserved.
/// @author Max Voronkov <maxim.voronkov@csiro.au>
///

#include "MEDataIterator.h"
#include "MEDataAccessor.h"

namespace conrad {

/// MEDataIterator is an abstract class defining the interface
/// Only some trivial methods are defined here

/// an empty virtual destructor to make the compiler happy
MEDataIterator::~MEDataIterator()
{
}
	
/// operator-> delivers a pointer. 
/// @return a pointer to the current chunk
/// Allows the syntax like it->visibility()
/// The default implementation works via operator*, however to 
/// avoid an additional function call, the method
/// can be specialized in the derived classes
const MEDataAccessor* MEDataIterator::operator->() const
{
  return &(operator*());
}

/// Checks whether the iterator reached an end.
/// @return True if the iterator has reached an end. 
/// The Default implementation works via hasMore(), however 
/// one can override the method in a derived class to avoid 
/// this (slight) overhead
casa::Bool MEDataIterator::atEnd() const throw()
{ 
  return !hasMore();
}

/// advance the iterator one step further
/// @return A reference to itself (to allow ++++it synthax)
/// The default implementation is via next(), however one can
/// override this method in a derived class to avoid this (slight)
/// overhead
MEDataIterator& MEDataIterator::operator++(int)
{
  next();
  return *this;
}

} // namespace conrad
