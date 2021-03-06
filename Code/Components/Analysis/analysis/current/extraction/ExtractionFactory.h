/// @file ExtractionFactory.h
///
/// Front end handler to deal with all the different types of spectrum/image/cube extraction
///
/// @copyright (c) 2011 CSIRO
/// Australia Telescope National Facility (ATNF)
/// Commonwealth Scientific and Industrial Research Organisation (CSIRO)
/// PO Box 76, Epping NSW 1710, Australia
/// atnf-enquiries@csiro.au
///
/// This file is part of the ASKAP software distribution.
///
/// The ASKAP software distribution is free software: you can redistribute it
/// and/or modify it under the terms of the GNU General Public License as
/// published by the Free Software Foundation; either version 2 of the License,
/// or (at your option) any later version.
///
/// This program is distributed in the hope that it will be useful,
/// but WITHOUT ANY WARRANTY; without even the implied warranty of
/// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
/// GNU General Public License for more details.
///
/// You should have received a copy of the GNU General Public License
/// along with this program; if not, write to the Free Software
/// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
///
/// @author Matthew Whiting <Matthew.Whiting@csiro.au>
///
#ifndef ASKAP_ANALYSIS_EXTRACTION_FACTORY_H_
#define ASKAP_ANALYSIS_EXTRACTION_FACTORY_H_

//System includes
#include <vector>

//ASKAP includes
#include <askapparallel/AskapParallel.h>
#include <sourcefitting/RadioSource.h>

//3rd-party includes
#include <Common/ParameterSet.h>
#include <duchamp/param.hh>

namespace askap {

    namespace analysis {

	/// @brief A class to handle multiple types of extraction
	/// @details This class provides the capability to extract
	/// different types of data products for each RadioSource
	/// object, including spectra, noise spectra, cubelets and
	/// moment maps. It makes use of the ParameterSet interface to
	/// decide which types are required, and can use the
	/// AskapParallel functionality to distribute the work over
	/// the available worker nodes.
	class ExtractionFactory
	{
	public:
	    ExtractionFactory(askap::askapparallel::AskapParallel& comms, const LOFAR::ParameterSet& parset);
	    virtual ~ExtractionFactory(){};

	    /// @brief Set the params - used principally for object selection
	    void setParams(duchamp::Param &par){itsParam = &par;};
	    /// @brief Set the full list of detected sources 
	    void setSourceList(std::vector<sourcefitting::RadioSource> &srclist){itsSourceList = srclist;};

	    /// @brief Distribute the source list across available worker nodes
	    void distribute();
	    /// @brief Extract the requested data products
	    void extract();

	protected:
	    // Class for communications
	    askap::askapparallel::AskapParallel& itsComms;
	    LOFAR::ParameterSet itsParset;

	    std::vector<sourcefitting::RadioSource> itsSourceList;
	    std::vector<bool> itsObjectChoice;

	    duchamp::Param *itsParam;

	};


    }

}



#endif //#ifndef ASKAP_ANALYSIS_EXTRACTION_FACTORY_H_
