/// @file
///
/// ComponentEquation: Equation for dealing with discrete components such
/// as point sources and Gaussians.
///
/// (c) 2007 CONRAD, All Rights Reserved.
/// @author Tim Cornwell <tim.cornwell@csiro.au>
///

#ifndef SYNCOMPONENTEQUATION_H_
#define SYNCOMPONENTEQUATION_H_

// own include
#include <fitting/Equation.h>
#include <fitting/Params.h>
#include <fitting/DesignMatrix.h>


#include <dataaccess/SharedIter.h>
#include <dataaccess/IDataIterator.h>
#include <dataaccess/CachedAccessorField.tcc>

#include <measurementequation/IParameterizedComponent.h>
#include <measurementequation/IUnpolarizedComponent.h>

// casa includes
#include <casa/aips.h>
#include <casa/Arrays/Array.h>
#include <casa/Arrays/Vector.h>
#include <casa/Arrays/Matrix.h>
#include <casa/Arrays/Cube.h>

namespace conrad
{
  namespace synthesis
  {

    /// @brief Visibility processing for components
    ///
    /// @details This class does predictions and calculates normal equations
    /// for discrete components such as point sources and Gaussians.
    /// Names are flux.{i,q,u,v}, direction.{ra,dec}, shape.{bmaj,bmin,bpa}
    /// etc.
    ///
    /// @ingroup measurementequation
    
    class ComponentEquation : public conrad::scimath::Equation
    {
      public:

        /// @brief Standard constructor using the parameters and the
        /// data iterator.
        /// @param ip Parameters
        /// @param idi data iterator
        ComponentEquation(const conrad::scimath::Params& ip,
          IDataSharedIter& idi);

        /// @brief Constructor using default parameters
        /// @param idi data iterator
        ComponentEquation(IDataSharedIter& idi);
        
        /// Return the default parameters
        static conrad::scimath::Params defaultParameters();

        /// @brief Predict model visibility for the iterator. 
        virtual void predict();

/// @brief Calculate the normal equations
/// @param ne Normal equations
        virtual void calcEquations(conrad::scimath::NormalEquations& ne);

      private:
      /// Shared iterator for data access
        IDataSharedIter itsIdi;
        /// Initialize this object
        virtual void init();
    protected:
        /// a short cut to shared pointer on a parameterized component
        typedef boost::shared_ptr<IParameterizedComponent> IParameterizedComponentPtr;
    
        /// @brief fill the cache of the components
        /// @details This method convertes the parameters into a vector of 
        /// components. It is called on the first access to itsComponents
        void fillComponentCache(std::vector<IParameterizedComponentPtr> &in) const;      
        
        /// @brief a helper method to populate a visibility cube
        /// @details This is method computes visibilities for the one given
        /// component and adds them to the cube provided. This is the most
        /// generic method, which iterates over polarisations. An overloaded
        /// version of the method do the same for unpolarised components
        /// (i.e. it doesn't bother to add zeros)
        ///
        /// @param[in] comp component to generate the visibilities for
        /// @param[in] uvw baseline spacings, one triplet for each data row.
        /// @param[in] freq a vector of frequencies (one for each spectral
        ///            channel) 
        /// @param[in] rwVis a non-const reference to the visibility cube to alter
        static void addModelToCube(const IParameterizedComponent& comp,
               const casa::Vector<casa::RigidVector<casa::Double, 3> > &uvw,
               const casa::Vector<casa::Double>& freq,
               casa::Cube<casa::Complex> &rwVis);

        /// @brief a helper method to populate a visibility cube
        /// @details This is method computes visibilities for the one given
        /// component and adds them to the cube provided. This is a second
        /// version of the method. It is intended for unpolarised components
        /// (i.e. it doesn't bother to add zeros)
        ///
        /// @param[in] comp component to generate the visibilities for
        /// @param[in] uvw baseline spacings, one triplet for each data row.
        /// @param[in] freq a vector of frequencies (one frequency for each 
        ///            spectral channel) 
        /// @param[in] rwVis a non-const reference to the visibility cube to alter
        static void addModelToCube(const IUnpolarizedComponent& comp,
               const casa::Vector<casa::RigidVector<casa::Double, 3> > &uvw,
               const casa::Vector<casa::Double>& freq,
               casa::Cube<casa::Complex> &rwVis);
        
        /// @brief a helper method to update design matrix and residuals
        /// @details This method iterates over a given number of polarisation 
        /// products in the visibility cube. It updates the design matrix with
        /// derivatives and subtracts values from the vector of residuals.
        /// The latter is a flattened vector which should have a size of 
        /// 2*nChan*nPol*nRow. Spectral channel is the most frequently varying
        /// index, then follows the polarisation index, and the least frequently
        /// varying index is the row. The number of channels and the number of
        /// rows always corresponds to that of the visibility cube. The number of
        /// polarisations can be less than the number of planes in the cube to
        /// allow processing of incomplete data cubes (or unpolarised components). In contrast to 
        /// 
        /// @param[in] comp component to generate the visibilities for
        /// @param[in] uvw baseline coorindates for each row
        /// @param[in] freq a vector of frequencies (one frequency for each
        ///            spectral channel)
        /// @param[in] dm design matrix to update (to add derivatives to)
        /// @param[in] residual vector of residuals to update 
        /// @param[in] nPol a number of polarisation products to process
        static void updateDesignMatrixAndResiduals(
                   const IParameterizedComponent& comp,
                   const casa::Vector<casa::RigidVector<casa::Double, 3> > &uvw,
                   const casa::Vector<casa::Double>& freq,
                   scimath::DesignMatrix &dm, casa::Vector<casa::Double> &residual,
                   casa::uInt nPol);

    private:   
        /// @brief vector of components plugged into this component equation
        /// this has nothing to do with data accessor, we just reuse the class
        /// for a cached field
        CachedAccessorField<std::vector<IParameterizedComponentPtr> > itsComponents;     
        
        /// @brief True if all components are unpolarised
        mutable bool itsAllComponentsUnpolarised;
    };

  }

}

#endif
