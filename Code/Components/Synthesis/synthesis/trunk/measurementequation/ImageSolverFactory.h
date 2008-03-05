/// @file
///
/// ImageSolverFactory: Factory class for image solvers
///
/// (c) 2007 ASKAP, All Rights Reserved.
/// @author Tim Cornwell <tim.cornwell@csiro.au>
///
#ifndef ASKAP_SYNTHESIS_IMAGESOLVERFACTORY_H_
#define ASKAP_SYNTHESIS_IMAGESOLVERFACTORY_H_

#include <fitting/Solver.h>
#include <fitting/Params.h>

#include <APS/ParameterSet.h>

namespace askap
{
  namespace synthesis
  {
    /// @brief Construct image solvers according to parameters
    /// @ingroup measurementequation
    class ImageSolverFactory
    {
      public:
        ImageSolverFactory();
        
        ~ImageSolverFactory();

        /// @brief Make a shared pointer for an image solver
        /// @param ip Params for the solver
        /// @param parset ParameterSet containing description of
        /// solver to be constructed
        static askap::scimath::Solver::ShPtr make(askap::scimath::Params& ip, 
          const LOFAR::ACC::APS::ParameterSet& parset); 
    };

  }
}
#endif
