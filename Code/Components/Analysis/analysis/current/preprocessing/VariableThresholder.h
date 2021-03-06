/// @file
///
/// Control class to run the calculation of the variable (box) threshold
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
#ifndef ASKAP_ANALYSIS_VAR_THRESH_H_
#define ASKAP_ANALYSIS_VAR_THRESH_H_
#include <askapparallel/AskapParallel.h>
#include <analysisparallel/SubimageDef.h>
#include <outputs/ImageWriter.h>
#include <Common/ParameterSet.h>
#include <duchamp/Cubes/cubes.hh>
#include <string>
#include <casa/Arrays/Array.h>
#include <casa/Arrays/IPosition.h>
#include <casa/Arrays/Slicer.h>
#include <coordinates/Coordinates/CoordinateSystem.h>
#include <casa/aipstype.h>
#include <parallelanalysis/Weighter.h>

namespace askap {

    namespace analysis {

	/// @brief Handle the calculation and application of a
	/// threshold that varies with location in the image.

	/// @details This class handles all operations related to the
	/// calculation and application of a variable detection
	/// threshold, as well as the output of maps of the threshold,
	/// noise, S/N ratio. The threshold is calculated based on the
	/// statistics within a sliding box, so that the noise
	/// properties for a given pixel depend only on the pixels
	/// within a box (2D or 1D) of a specified size centred on
	/// that pixel. The statistics can be calculated based on
	/// robust measures (median and MADFM), or traditional
	/// mean/standard deviation. The threshold applied is a
	/// constant signal-to-noise ratio.
	///
	/// The maps of various quantities can also be written to CASA images on disk. These quantities include the noise level, the threshold (in flux units), the signal-to-noise ratio

	class VariableThresholder
	{
	public:
	    VariableThresholder(){};
	    VariableThresholder(askap::askapparallel::AskapParallel& comms, const LOFAR::ParameterSet &parset);
	    VariableThresholder(const VariableThresholder& other);
	    VariableThresholder& operator= (const VariableThresholder& other);
	    virtual ~VariableThresholder(){};

	    void initialise(duchamp::Cube &cube, analysisutilities::SubimageDef &subdef);
	    void setWeighter(Weighter *weighter){itsWeighter = weighter;};
	    void setFilenames(askap::askapparallel::AskapParallel& comms);
	    void calculate();
	    void search();

	    std::string snrImage(){return itsSNRimageName;};
	    std::string thresholdImage(){return itsThresholdImageName;};
	    std::string noiseImage(){return itsNoiseImageName;};
	    std::string averageImage(){return itsAverageImageName;};
	    std::string boxSumImage(){return itsBoxSumImageName;};

	    int boxSize(){return itsBoxSize;};

	protected:
	    void writeImages(casa::Array<casa::Float> &middle, casa::Array<casa::Float> &spread, casa::Array<casa::Float> &snr, casa::Array<casa::Float> &boxsum, casa::IPosition &loc, bool doCreate);
	    /* void writeArray(ImageWriter &writer, casa::Array<casa::Float> &array, casa::IPosition &loc); */
	    void defineChunk(casa::Array<Float> &inputChunkArr, casa::MaskedArray<Float> &outputChunk, size_t ctr);
	    void saveSNRtoCube(casa::Array<casa::Float> &snr, size_t ctr);

	    /// @brief The MPI communication information
	    askap::askapparallel::AskapParallel *itsComms;

	    /// @brief The defining parset
	    LOFAR::ParameterSet itsParset;

	    /// Should we use robust (ie. median-based) statistics
	    bool itsFlagRobustStats;
	    float itsSNRthreshold;
	    std::string itsSearchType;
	    /// The half-box-width used for the sliding-box calculations
	    int itsBoxSize;

	    std::string itsInputImage;

	    /// Name of S/N image to be written
	    std::string itsSNRimageName;
	    /// Name of Threshold image to be written
	    std::string itsThresholdImageName;
	    /// Name of Noise image to be written
	    std::string itsNoiseImageName;
	    /// Name of Mean image to be written
	    std::string itsAverageImageName;
	    /// Name of box sum image to be written
	    std::string itsBoxSumImageName;
	    /// Do we need to write any images?
	    bool doWriteImages;
	    /// Are we re-using exising images?
	    bool itsFlagReuse;
	    
	    /// @brief The subimage definition
	    analysisutilities::SubimageDef *itsSubimageDef;
	    duchamp::Cube *itsCube;
	    Weighter *itsWeighter;
	    casa::Slicer itsSlicer;
	    casa::IPosition itsInputShape;
	    casa::IPosition itsLocation;
	    casa::CoordinateSystem itsInputCoordSys;

	};

    }

}




#endif
