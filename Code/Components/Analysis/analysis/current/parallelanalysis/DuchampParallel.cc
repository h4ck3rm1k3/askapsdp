/// @file
///
/// @brief Base class for parallel applications
/// @details
/// Supports algorithms by providing methods for initialization
/// of MPI connections, sending and models around.
/// There is assumed to be one master and many workers.
///
/// @copyright (c) 2007 CSIRO
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
/// @author Matthew Whiting <matthew.whiting@csiro.au>
///

#include <Common/LofarTypedefs.h>
#include <Common/ParameterSet.h>
#include <Common/KVpair.h>
using namespace LOFAR::TYPES;
#include <Blob/BlobString.h>
#include <Blob/BlobIBufString.h>
#include <Blob/BlobOBufString.h>
#include <Blob/BlobIStream.h>
#include <Blob/BlobOStream.h>
#include <Common/Exceptions.h>

#include <casa/OS/Timer.h>
#include <casa/Utilities/Regex.h>
#include <casa/BasicSL/String.h>
#include <casa/OS/Path.h>
#include <images/Images/ImageOpener.h>
#include <images/Images/FITSImage.h>
#include <images/Images/MIRIADImage.h>
#include <images/Images/SubImage.h>
#include <casa/aipstype.h>
#include <casa/Arrays/Array.h>
#include <casa/Arrays/ArrayLogical.h>
#include <casa/Arrays/ArrayPartMath.h>
#include <casa/Arrays/Vector.h>
#include <casa/Arrays/IPosition.h>
#include <casa/Arrays/Slicer.h>

// boost includes
#include <boost/shared_ptr.hpp>

#include <askap_analysis.h>

#include <askap/AskapLogging.h>
#include <askap/AskapError.h>

#include <askapparallel/AskapParallel.h>
#include <imageaccess/CasaImageAccess.h>

#include <parallelanalysis/DuchampParallel.h>
#include <parallelanalysis/Weighter.h>
#include <parallelanalysis/ParallelStats.h>
#include <parallelanalysis/ObjectParameteriser.h>
#include <preprocessing/VariableThresholder.h>
#include <extraction/ExtractionFactory.h>
#include <analysisutilities/AnalysisUtilities.h>
#include <sourcefitting/RadioSource.h>
#include <sourcefitting/FittingParameters.h>
#include <sourcefitting/CurvatureMapCreator.h>
#include <parametrisation/OptimisedGrower.h>
#include <preprocessing/Wavelet2D1D.h>
#include <outputs/AskapAsciiCatalogueWriter.h>
#include <outputs/AskapComponentParsetWriter.h>
#include <outputs/AskapVOTableCatalogueWriter.h>
#include <outputs/ImageWriter.h>

#include <casainterface/CasaInterface.h>
#include <analysisparallel/SubimageDef.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <time.h>
#include <vector>
#include <map>

#include <duchamp/duchamp.hh>
#include <duchamp/param.hh>
#include <duchamp/Cubes/cubes.hh>
#include <duchamp/Utils/Statistics.hh>
#include <duchamp/Utils/utils.hh>
#include <duchamp/Detection/detection.hh>
// #include <duchamp/Detection/columns.hh>
#include <duchamp/Outputs/columns.hh>
#include <duchamp/Outputs/CatalogueSpecification.hh>
#include <duchamp/Outputs/KarmaAnnotationWriter.hh>
#include <duchamp/Outputs/DS9AnnotationWriter.hh>
#include <duchamp/Outputs/CasaAnnotationWriter.hh>
#include <duchamp/PixelMap/Voxel.hh>
#include <duchamp/PixelMap/Object3D.hh>

///@brief Where the log messages go.
ASKAP_LOGGER(logger, ".parallelanalysis");

using namespace std;
using namespace askap;
using namespace askap::askapparallel;
using namespace askap::analysisutilities;

using namespace duchamp;

namespace askap {
    namespace analysis {

      void reportDim(size_t *dim, size_t size)
      {

	std::stringstream ss;
	for (size_t i = 0; i < size; i++) {
	  ss << dim[i];
	  if(i < size-1) ss << " x ";
	}
	
	ASKAPLOG_INFO_STR(logger, "Dimensions of input image = " << ss.str());
	
      }

        //**************************************************************//

        bool DuchampParallel::is2D()
        {
            /// @details Check whether the image is 2-dimensional, by
            /// looking at the dim array in the Cube object, and counting
            /// the number of dimensions that are greater than 1
            /// @todo Make use of the new Cube::is2D() function.
            int numDim = 0;
            size_t *dim = this->itsCube.getDimArray();

            for (int i = 0; i < this->itsCube.getNumDim(); i++) if (dim[i] > 1) numDim++;

            return numDim <= 2;
        }

        //**************************************************************//


        DuchampParallel::DuchampParallel(askap::askapparallel::AskapParallel& comms)
                : itsComms(comms)
        {
            this->itsFitParams = sourcefitting::FittingParameters(LOFAR::ParameterSet());
	    this->itsWeighter = new Weighter(this->itsComms,LOFAR::ParameterSet());
	    this->itsVarThresher = new VariableThresholder(this->itsComms,LOFAR::ParameterSet());
        }

        //**************************************************************//

        DuchampParallel::DuchampParallel(askap::askapparallel::AskapParallel& comms,
                                         const LOFAR::ParameterSet& parset)
	  : itsComms(comms), itsParset(parset)
        {
            /// @details The constructor reads parameters from the parameter
            /// set parset. This set can include Duchamp parameters, as well
            /// as particular Selavy parameters such as masterImage and
            /// sectionInfo.

	    ASKAPLOG_INFO_STR(logger, "Initialising parallel finder, based on Duchamp v" << duchamp::VERSION);

	    this->deprecatedParameters();

            // First do the setup needed for both workers and master
            this->itsCube.pars() = parseParset(this->itsParset);
            ImageOpener::ImageTypes imageType = ImageOpener::imageType(this->itsCube.pars().getImageFile());
            this->itsIsFITSFile = (imageType == ImageOpener::FITS);
	    bool useCasa = this->itsParset.getBool("useCASAforFITS",true);
	    this->itsIsFITSFile = this->itsIsFITSFile && !useCasa;
	    if(this->itsIsFITSFile) ASKAPLOG_DEBUG_STR(logger, "Using the Duchamp FITS-IO tasks");

	    bool flagSubsection = this->itsParset.getBool("flagSubsection",false);
	    this->itsBaseSubsection = this->itsParset.getString("subsection","");
	    if(!flagSubsection) this->itsBaseSubsection = "";
	    else ASKAPLOG_DEBUG_STR(logger, "Requested subsection " << this->itsBaseSubsection);
	    if(this->itsBaseSubsection=="") this->itsBaseSubsection = duchamp::nullSection( getCASAdimensions(this->itsCube.pars().getImageFile()).size() );

	    this->itsBaseStatSubsection = this->itsParset.getBool("flagStatSec",false) ? this->itsParset.getString("statSec","") : "" ;

	    this->itsFlagThresholdPerWorker = this->itsParset.getBool("thresholdPerWorker",false);
	    
	    this->itsWeighter = new Weighter(this->itsComms, this->itsParset.makeSubset("Weights."));

            this->itsFlagVariableThreshold = this->itsParset.getBool("VariableThreshold", false);
	    this->itsVarThresher = new VariableThresholder(this->itsComms,this->itsParset.makeSubset("VariableThreshold."));
	    // this->itsVarThresher->setFilenames(this->itsComms);

	    this->itsFlagOptimiseMask = this->itsParset.getBool("optimiseMask",false);

	    this->itsFlagWavelet2D1D = this->itsParset.getBool("recon2D1D",false);
	    this->itsCube.pars().setFlagATrous(this->itsCube.pars().getFlagATrous() || this->itsFlagWavelet2D1D);

            LOFAR::ParameterSet fitParset = this->itsParset.makeSubset("Fitter.");
            this->itsFitParams = sourcefitting::FittingParameters(fitParset);
	    this->itsFlagDistribFit = this->itsParset.getBool("distribFit",true);

            this->itsFlagFindSpectralTerms = this->itsParset.getBoolVector("findSpectralTerms", std::vector<bool>(2,this->itsFitParams.doFit()));
	    for(size_t i=this->itsFlagFindSpectralTerms.size();i<2;i++) this->itsFlagFindSpectralTerms.push_back(false);
	    this->itsSpectralTermImages = this->itsParset.getStringVector("spectralTermImages", std::vector<std::string>(2, ""));
	    for(size_t i=this->itsSpectralTermImages.size();i<2;i++) this->itsSpectralTermImages.push_back("");
	    if(this->itsFlagFindSpectralTerms[0]){
		if(!this->itsFitParams.doFit()){
		    ASKAPLOG_WARN_STR(logger, "No fitting is to be done, so the spectral indices will not be found. Setting findSpectralIndex=false.");
		    this->itsFlagFindSpectralTerms = std::vector<bool>(2,false);
		}
		else{
		    this->checkSpectralTermImages();
		}
	    }
	    else this->itsFlagFindSpectralTerms[1]=false;
	    
	    this->itsFlagExtractSpectra = this->itsParset.getBool("extractSpectra",false);
	    if(this->itsFlagExtractSpectra){
		if(!this->itsParset.isDefined("extractSpectra.spectralCube")){
		    ASKAPLOG_WARN_STR(logger, "Source cube not defined for extracting spectra. Please use the \"spectralCube\" parameter. Turning off spectral extraction.");
		    this->itsFlagExtractSpectra = false;
		    this->itsParset.replace("extractSpectra","false");
		}
		else ASKAPLOG_INFO_STR(logger, "Extracting spectra for detected sources from " << this->itsParset.getString("extractSpectra.spectralCube",""));
	    }
	    this->itsFlagExtractNoiseSpectra = this->itsParset.getBool("extractNoiseSpectra",false);
	    if(this->itsFlagExtractNoiseSpectra){
		if(!this->itsParset.isDefined("extractNoiseSpectra.spectralCube")){
		    ASKAPLOG_WARN_STR(logger, "Source cube not defined for extracting noise spectra. Please use the \"spectralCube\" parameter. Turning off noise spectra extraction.");
		    this->itsFlagExtractNoiseSpectra = false;
		    this->itsParset.replace("extractNoiseSpectra","false");
		}
		else ASKAPLOG_INFO_STR(logger, "Extracting noise spectra for detected sources from " << this->itsParset.getString("extractNoiseSpectra.spectralCube",""));
	    }

	    this->itsFitSummaryFile = this->itsParset.getString("fitResultsFile","selavy-fitResults.txt");
	    this->itsFitAnnotationFile = this->itsParset.getString("fitAnnotationFile","selavy-fitResults.ann");
	    this->itsFitBoxAnnotationFile = this->itsParset.getString("fitBoxAnnotationFile","selavy-fitResults.boxes.ann");

	    this->itsSubimageAnnotationFile = this->itsParset.getString("subimageAnnotationFile", "selavy-SubimageLocations.ann");

            if (itsComms.isParallel()) {
		this->itsSubimageDef = SubimageDef(this->itsParset);
		int ovx=this->itsSubimageDef.overlapx();
		int ovy=this->itsSubimageDef.overlapy();
		int ovz=this->itsSubimageDef.overlapz();

		// Need the overlap to be at least the boxPadSize used by the Fitting
		if(this->itsFitParams.doFit()){
		    if(this->itsSubimageDef.nsubx()>1)
			this->itsSubimageDef.setOverlapX(std::max(this->itsSubimageDef.overlapx(), this->itsFitParams.boxPadSize()));
		    if(this->itsSubimageDef.nsuby()>1)
			this->itsSubimageDef.setOverlapY(std::max(this->itsSubimageDef.overlapy(), this->itsFitParams.boxPadSize()));
		    // Don't need to change overlapz, as the fitting box only affects the spatial directions
		}
	      
		// Need the overlap to be at least the full box width so we get full coverage in the variable threshold case.
		if(this->itsFlagVariableThreshold){
		    if(this->itsCube.pars().getSearchType()=="spatial"){
			if(this->itsSubimageDef.nsubx()>1)
			    this->itsSubimageDef.setOverlapX(std::max(this->itsSubimageDef.overlapx(), 2*this->itsVarThresher->boxSize()+1));
			if(this->itsSubimageDef.nsuby()>1)
			    this->itsSubimageDef.setOverlapY(std::max(this->itsSubimageDef.overlapy(), 2*this->itsVarThresher->boxSize()+1));
		    }
		    else{
			if(this->itsSubimageDef.nsubz()>1)
			    this->itsSubimageDef.setOverlapZ(std::max(this->itsSubimageDef.overlapz(), 2*this->itsVarThresher->boxSize()));
		    }
		}

		// If values have changed, alert user and update parset.
		if((this->itsSubimageDef.overlapx()!=ovx)||(this->itsSubimageDef.overlapy()!=ovy)||(this->itsSubimageDef.overlapz()!=ovz)){
		    ASKAPLOG_INFO_STR(logger, "Changed Subimage overlaps to " << this->itsSubimageDef.overlapx() << ","
				       << this->itsSubimageDef.overlapy() << ","<< this->itsSubimageDef.overlapz());
		    this->itsParset.replace(LOFAR::KVpair("overlapx",this->itsSubimageDef.overlapx()));
		    this->itsParset.replace(LOFAR::KVpair("overlapy",this->itsSubimageDef.overlapy()));
		    this->itsParset.replace(LOFAR::KVpair("overlapz",this->itsSubimageDef.overlapz()));
		}
	    }
	    else {
		this->itsSubimageDef = SubimageDef();
	    }		
	    
	}

	void DuchampParallel::checkAndWarn(std::string oldParam, std::string newParam)
	{
	    /// @details A utility function to check for the existence
	    /// in the parset of an out-of-date parameter (ie. one
	    /// that has been deprecated). If it is present, a warning
	    /// message is displayed, and, if it has been renamed this
	    /// is also conveyed to the user. If the renamed parameter
	    /// is not present in the parset, then it is assigned the
	    /// value taken by the old one.
	    /// @param oldParam The old, deprecated parameter name
	    /// @param newParam The new parameter name. If there is no
	    /// equivalent, give this as "".
	    
	    if(this->itsParset.isDefined(oldParam)){
		if(newParam == "" ){  // there is no equivalent anymore
		    ASKAPLOG_WARN_STR(logger, "The parameter \""<<oldParam<<"\" has been deprecated and has no equivalent. Remove it from your parset!");
		}
		else {
		    if(!this->itsParset.isDefined(newParam)){
			std::string val=this->itsParset.getString(oldParam);
			ASKAPLOG_WARN_STR(logger, "The parameter \"" << oldParam <<"\" should now be given as \""<<newParam<<"\". Setting this to " << val << ", but you should change your parset!");
			this->itsParset.replace(newParam,val);
		    }
		    else{
			ASKAPLOG_WARN_STR(logger, "The parameter \"" << oldParam <<"\" should now be given as \""<<newParam<<"\". Your parset has this defined, so no change is made, but you should remove "<<oldParam<<" from your parset.");
		    }
		}
	    }
	}

	void DuchampParallel::deprecatedParameters()
	{

	    /// @details A check is made for the presence in the
	    /// parset of parameters that have been deprecated. The
	    /// parset is updated if need be according to the rules
	    /// for DuchampParallel::checkAndWarn().

	    this->checkAndWarn("doFit","Fitter.doFit");
	    this->checkAndWarn("fitJustDetection", "Fitter.fitJustDetection");
	    this->checkAndWarn("doMedianSearch","VariableThreshold");
	    this->checkAndWarn("medianBoxWidth","VariableThreshold.boxSize");
	    this->checkAndWarn("flagWriteSNRimage","");
	    this->checkAndWarn("SNRimageName","VariableThreshold.SNRimageName");
	    this->checkAndWarn("flagWriteThresholdImage","");
	    this->checkAndWarn("ThresholdImageName","VariableThreshold.ThresholdImageName");
	    this->checkAndWarn("flagWriteNoiseImage","");
	    this->checkAndWarn("NoiseImageName","VariableThreshold.NoiseImageName");
	    this->checkAndWarn("weightsimage","Weights.weightsImage");
	}


	void DuchampParallel::checkSpectralTermImages()
	{
	    /// @details Once the parameters relating to the spectral
	    /// index & curvature images have been read, we need to
	    /// check to see if the images need to be specified.

	    std::string termname[3]={".taylor.1",".taylor.2"};

	    for(size_t i=0;i<2;i++){

		if(this->itsFlagFindSpectralTerms[i]){

		    if(this->itsSpectralTermImages[i] == "") {  
			// if it hasn't been specified, set it to the .taylor.n image, but only if the input is a .taylor.0 image
			size_t pos = this->itsCube.pars().getImageFile().rfind(".taylor.0");
			if (pos == std::string::npos) {
			    // image provided is not a Taylor series term - notify and do nothing
			    ASKAPLOG_WARN_STR(logger, "Image name provided (" << this->itsCube.pars().getImageFile() 
					      << ") is not a Taylor term. Cannot find spectral information.");

			    // set flag for this and higher terms to false
			    for(size_t j=i;j<2;j++) this->itsFlagFindSpectralTerms[j] = false;
			    
			}
			else { // it is a taylor.0 image, so set current term's image appropriately
			    this->itsSpectralTermImages[i] = this->itsCube.pars().getImageFile();
			    this->itsSpectralTermImages[i].replace(pos,9,termname[i]);
			}
			
		    }

		}
		
	    }

	}

        //**************************************************************//

	void DuchampParallel::setSubimageDefForFITS()
	{
	    /// @details This utility function sets up the SubimageDef
	    /// object appropriate for the case that we are accessing
	    /// a FITS file. Upon completion, the SubimageDef object
	    /// will have its image name, subsection string, image
	    /// dimensions and nsub/overlap parameters defined. If no
	    /// subsectioning is required, the subsection string in
	    /// the cube parameters will be set to the null subsection
	    /// of appropriate dimensionality.

	    this->itsSubimageDef.defineFITS(this->itsCube.pars().getImageFile());
	    this->itsSubimageDef.setImage(this->itsCube.pars().getImageFile());
	    this->itsSubimageDef.setInputSubsection(this->itsBaseSubsection);
	    std::vector<size_t> dim = getFITSdimensions(this->itsCube.pars().getImageFile());
	    reportDim(dim.data(),dim.size());
	    this->itsSubimageDef.setImageDim(dim);

	    if (!this->itsCube.pars().getFlagSubsection() || this->itsCube.pars().getSubsection() == "") {
		this->itsCube.pars().setFlagSubsection(true);
		this->itsCube.pars().setSubsection(nullSection(this->itsSubimageDef.getImageDim().size()));
	    }

	}

        //**************************************************************//

        int DuchampParallel::getMetadata()
        {
            /// @details Provides a simple front-end to the correct
            /// metadata-reading function, according to whether the image is
            /// FITS data or a CASA image
            /// @return The return value of the function that was used:
            /// either duchamp::SUCCESS or duchamp::FAILURE
	    int returnCode;
            if (this->itsIsFITSFile){

		this->setSubimageDefForFITS();

                    if (this->itsCube.pars().verifySubsection() == duchamp::FAILURE)
                        ASKAPTHROW(AskapError, this->workerPrefix() << "Cannot parse the subsection string " << this->itsCube.pars().getSubsection());

                    returnCode = this->itsCube.getMetadata();
		    if(returnCode == duchamp::FAILURE) {
		      ASKAPTHROW(AskapError, this->workerPrefix() << "Something went wrong with itsCube.getMetadata()");
		    }
		    
                    // check the true dimensionality and set the 2D flag in the cube header.
                    int numDim = 0;
                    size_t *dim = this->itsCube.getDimArray();

                    for (int i = 0; i < this->itsCube.getNumDim(); i++) if (dim[i] > 1) numDim++;

                    this->itsCube.header().set2D(numDim <= 2);

                    // set up the various flux units
                    if (this->itsCube.header().getWCS()->spec >= 0) this->itsCube.header().fixSpectralUnits(this->itsCube.pars().getSpectralUnits());

	    }
            // else return casaImageToMetadata(this->itsCube, this->itsSubimageDef, itsComms);
            else returnCode = this->getCASA(METADATA,false);

	    return returnCode;
        }

        //**************************************************************//

        std::vector<float> DuchampParallel::getBeamInfo()
        {
            /// @details Returns a vector containing the beam parameters:
            /// major axis [deg], minor axis [deg], position angle [deg].
            std::vector<float> beam(3);
	    beam[0] = this->itsCube.header().beam().maj();
	    beam[1] = this->itsCube.header().beam().min();
	    beam[2] = this->itsCube.header().beam().pa();
	    return beam;
        }
        //**************************************************************//


        void DuchampParallel::readData()
        {
            /// @details Reads in the data to the duchamp::Cube class. For
            /// the workers, this either uses the duchamp functionality, in
            /// the case of FITS data, or calls the routines in
            /// CasaInterface.cc in the case of casa (or other) formats. If
            /// reconstruction or smoothing are required, they are done in
            /// this function. For the master, the metadata only is read
            /// from the file, with the same choice based on the FITS status
            /// of the data file.
            if (itsComms.isParallel() && itsComms.isMaster()) {
                
                ASKAPLOG_INFO_STR(logger,  this->workerPrefix() << "About to read metadata from image " << this->itsCube.pars().getImageFile());

		int result=this->getMetadata();

                ASKAPLOG_INFO_STR(logger, "Annotation file for subimages is \"" << this->itsSubimageAnnotationFile << "\".");

                if (this->itsSubimageAnnotationFile != "") {
                    ASKAPLOG_INFO_STR(logger, "Writing annotation file showing subimages to " << this->itsSubimageAnnotationFile);
                    this->itsSubimageDef.writeAnnotationFile(this->itsSubimageAnnotationFile, this->itsCube.header(), this->itsCube.pars().getImageFile(), itsComms);
                }

                if (result == duchamp::FAILURE) {
                    ASKAPLOG_ERROR_STR(logger, this->workerPrefix() << "Could not read in metadata from image " << this->itsCube.pars().getImageFile() << ".");
                    ASKAPTHROW(AskapError, this->workerPrefix() << "Unable to read image " << this->itsCube.pars().getImageFile())
                } else {
                    ASKAPLOG_INFO_STR(logger,  this->workerPrefix() << "Read metadata from image " << this->itsCube.pars().getImageFile());
                }

                ASKAPLOG_INFO_STR(logger, this->workerPrefix() << "Dimensions are "
                                      << this->itsCube.getDimX() << " " << this->itsCube.getDimY() << " " << this->itsCube.getDimZ());

                if (this->itsCube.getDimZ() == 1) this->itsCube.pars().setMinChannels(0);

            } else if (itsComms.isWorker()) {

                int result;

                if (this->itsIsFITSFile) {

		    this->setSubimageDefForFITS();

                    if (itsComms.isParallel()) {
		      this->itsSubimageDef.setInputSubsection(this->itsBaseSubsection);
		      duchamp::Section subsection = this->itsSubimageDef.section(itsComms.rank()-1);
		      ASKAPLOG_DEBUG_STR(logger, this->workerPrefix()<<"Starting with base section = |"<<this->itsBaseSubsection
					 <<"| and node #"<<itsComms.rank()-1<<" we get section "<<subsection.getSection());
		      this->itsCube.pars().setFlagSubsection(true);
		      this->itsCube.pars().section()=subsection;
		      ASKAPLOG_INFO_STR(logger, this->workerPrefix() << "Subsection = " << this->itsCube.pars().section().getSection());
		      if(this->itsCube.pars().getFlagStatSec()){
			if(this->itsCube.pars().statsec().isValid())
			  ASKAPLOG_INFO_STR(logger, this->workerPrefix() << "Statistics section = " << this->itsCube.pars().statsec().getSection());
			else
			  ASKAPLOG_INFO_STR(logger, this->workerPrefix() << " does not contribute to the statistics section");
		      }
                    }
		    else{
		      this->itsCube.pars().setSubsection(this->itsBaseSubsection);
		      ASKAPLOG_INFO_STR(logger, this->workerPrefix() << "Subsection = " << this->itsCube.pars().section().getSection());
		    }

                    if (this->itsCube.pars().verifySubsection() == duchamp::FAILURE)
                        ASKAPTHROW(AskapError, this->workerPrefix() << "Cannot parse the subsection string " << this->itsCube.pars().getSubsection());

                    ASKAPLOG_INFO_STR(logger, this->workerPrefix() << "Using subsection " << this->itsCube.pars().getSubsection());
                    ASKAPLOG_INFO_STR(logger,  this->workerPrefix()
                                          << "About to read data from image " << this->itsCube.pars().getFullImageFile());

		    bool flag=this->itsCube.pars().getFlagATrous();
		    if(this->itsFlagVariableThreshold || this->itsWeighter->doScaling()) this->itsCube.pars().setFlagATrous(true);
                    result = this->itsCube.getCube();
		    if(this->itsFlagVariableThreshold || this->itsWeighter->doScaling()) this->itsCube.pars().setFlagATrous(flag);

                } else { // if it's a CASA image
		  result = getCASA(IMAGE);
                }

                if (result == duchamp::FAILURE) {
                    ASKAPLOG_ERROR_STR(logger, this->workerPrefix() << "Could not read in data from image " << this->itsCube.pars().getImageFile());
                    ASKAPTHROW(AskapError, this->workerPrefix() << "Unable to read image " << this->itsCube.pars().getImageFile());
                } else {
                    ASKAPLOG_INFO_STR(logger, this->workerPrefix() << "Dimensions are "
                                          << this->itsCube.getDimX() << " " << this->itsCube.getDimY() << " " << this->itsCube.getDimZ());

                    if (this->itsCube.getDimZ() == 1) this->itsCube.pars().setMinChannels(0);
                }

            }
        }

        //**************************************************************//

        void DuchampParallel::setupLogfile(int argc, const char** argv)
        {
            /// @details Opens the log file and writes the execution
            /// statement, the time, and the duchamp parameter set to it.
            if (this->itsCube.pars().getFlagLog()) {
		if(this->itsComms.isParallel()){
		    std::string inputLog=this->itsCube.pars().getLogFile();
		    size_t loc = inputLog.rfind(".");
		    std::string suffix = inputLog.substr(loc, inputLog.length());
		    std::string addition;
		    if(this->itsComms.isMaster()) addition=".Master";
		    else addition=itsComms.substitute(".%w");
		    if(loc != std::string::npos) this->itsCube.pars().setLogFile( inputLog.insert(loc,addition) );
		    else this->itsCube.pars().setLogFile( inputLog + addition );
		}
		else{
		    // In case the user has put %w in the logfile name but is running in serial mode
		    std::string inputLog=this->itsCube.pars().getLogFile();
		    size_t loc;
		    while (loc=inputLog.find("%w"), loc!=std::string::npos){
			inputLog.replace(loc,2,"");
		    }
		    while (loc=inputLog.find("%n"), loc!=std::string::npos){
			inputLog.replace(loc,2,"1");
		    }
		    this->itsCube.pars().setLogFile(inputLog);
		}
                ASKAPLOG_INFO_STR(logger, this->workerPrefix() << "Setting up logfile " << this->itsCube.pars().getLogFile());
                std::ofstream logfile(this->itsCube.pars().getLogFile().c_str());
                logfile << "New run of the Selavy sourcefinder: ";
                time_t now = time(NULL);
                logfile << asctime(localtime(&now));
                // Write out the command-line statement
                logfile << "Executing statement : ";

                for (int i = 0; i < argc; i++) logfile << argv[i] << " ";

                logfile << std::endl;
                logfile << this->itsCube.pars();
                logfile.close();
            }
        }

        //**************************************************************//

        void DuchampParallel::preprocess()
        {
	  /// @details Runs any requested pre-processing. This
	  /// includes inverting the cube, smoothing or
	  /// multi-resolution wavelet reconstruction.
	  /// This is only done on the worker nodes.

	  if(itsComms.isParallel() && itsComms.isMaster()){
	      if(this->itsWeighter->isValid() ){
		  this->itsWeighter->initialise(this->itsCube, !(itsComms.isParallel()&&itsComms.isMaster()));
	      }
	      if (this->itsFlagVariableThreshold) {
		  this->itsVarThresher->initialise(this->itsCube, this->itsSubimageDef);
		  this->itsVarThresher->calculate();
	      }
	      // If we are doing fitting, and want to use the curvature map, need to define/calculate this here.
	      if (this->itsFitParams.doFit() && this->itsFitParams.useCurvature()){
		
		CurvatureMapCreator curv(this->itsComms,this->itsParset.makeSubset("Fitter."));
		curv.initialise(this->itsCube, this->itsSubimageDef);
		ASKAPLOG_DEBUG_STR(logger, "Calling curv.write()");
		curv.write();
	      }
	  }	      

	  if(itsComms.isWorker()){

	    if(this->itsWeighter->isValid() ){
	      ASKAPLOG_INFO_STR(logger, this->workerPrefix() << "Preparing weights image");
	      this->itsWeighter->initialise(this->itsCube);
	      this->itsWeighter->applyCutoff();
	    }

	    if(this->itsCube.pars().getFlagNegative()){
	      ASKAPLOG_INFO_STR(logger, this->workerPrefix() << "Inverting cube");
	      this->itsCube.invert();
	    }
	    
	    if (this->itsFlagVariableThreshold) {
	      ASKAPLOG_INFO_STR(logger, this->workerPrefix() << "Defining the variable threshold maps");
	      this->itsVarThresher->initialise(this->itsCube, this->itsSubimageDef);
	      this->itsVarThresher->setWeighter(this->itsWeighter);
	      this->itsVarThresher->calculate();
	    }
	    else if( this->itsFlagWavelet2D1D ){
	      ASKAPLOG_INFO_STR(logger, this->workerPrefix() << "Reconstructing with the 2D1D wavelet algorithm");
	      Recon2D1D recon2d1d(this->itsParset.makeSubset("recon2D1D."));
	      recon2d1d.setCube(&this->itsCube);
	      recon2d1d.reconstruct();
	    }
	    else if (this->itsCube.pars().getFlagATrous()) {
	      ASKAPLOG_INFO_STR(logger,  this->workerPrefix() << "Reconstructing with dimension " << this->itsCube.pars().getReconDim());
	      this->itsCube.ReconCube();
	    } else if (this->itsCube.pars().getFlagSmooth()) {
	      ASKAPLOG_INFO_STR(logger,  this->workerPrefix() << "Smoothing");
	      this->itsCube.SmoothCube();
	    }
	    
	    // If we are doing fitting, and want to use the curvature map, need to define/calculate this here.
	    if (this->itsFitParams.doFit() && this->itsFitParams.useCurvature()){
		
		CurvatureMapCreator curv(this->itsComms,this->itsParset.makeSubset("Fitter."));
		curv.initialise(this->itsCube, this->itsSubimageDef);
		curv.calculate();
		this->itsFitParams.setSigmaCurv(curv.sigmaCurv());
		ASKAPLOG_DEBUG_STR(logger, "Fitting parameters now think sigma_curv is " << this->itsFitParams.sigmaCurv());
		curv.write();
	    }

	  }

	}

        //**************************************************************//

        void DuchampParallel::findSources()
        {
            /// @details Searches the image/cube for objects, using the
            /// appropriate search function given the user
            /// parameters. Merging of neighbouring objects is then done,
            /// and all WCS parameters are calculated.
            ///
            /// This is only done on the workers, although if we use
            /// the weight or variable-threshold search the master
            /// needs to do the initialisation of itsWeighter/itsVarThresher

            if (itsComms.isWorker()) {
                // remove mininum size criteria, so we don't miss anything on the borders.
                int minpix = this->itsCube.pars().getMinPix();
                int minchan = this->itsCube.pars().getMinChannels();
		int minvox = this->itsCube.pars().getMinVoxels();

                if (itsComms.isParallel()) {
                    this->itsCube.pars().setMinPix(1);
                    this->itsCube.pars().setMinChannels(1);
		    this->itsCube.pars().setMinVoxels(1);
                }


                if (this->itsCube.getSize() > 0) {
                    if (this->itsFlagVariableThreshold) {
                        ASKAPLOG_INFO_STR(logger, this->workerPrefix() << "Searching with a variable threshold");
			this->itsVarThresher->search();
		    } else if (this->itsWeighter->doScaling()){
		      ASKAPLOG_INFO_STR(logger, this->workerPrefix() << "Searching after weight scaling");
		      this->itsWeighter->search();
                    } else if (this->itsCube.pars().getFlagATrous()) {
                        ASKAPLOG_INFO_STR(logger,  this->workerPrefix() << "Searching with reconstruction first");
                        this->itsCube.ReconSearch();
                    } else if (this->itsCube.pars().getFlagSmooth()) {
                        ASKAPLOG_INFO_STR(logger,  this->workerPrefix() << "Searching with smoothing first");
                        this->itsCube.SmoothSearch();
                    } else {
                        ASKAPLOG_INFO_STR(logger,  this->workerPrefix() << "Searching, no smoothing or reconstruction done.");
                        this->itsCube.CubicSearch();
                    }
                }

		if(this->itsWeighter->isValid()) 
		  delete this->itsWeighter;

                ASKAPLOG_INFO_STR(logger,  this->workerPrefix() << "Intermediate list has " << this->itsCube.getNumObj() << " objects. Now merging.");

                // merge the objects, and grow them if necessary.
		this->itsCube.ObjectMerger();

                ASKAPLOG_INFO_STR(logger,  this->workerPrefix() << "Merged list has " << this->itsCube.getNumObj() << " objects.");

		if(this->itsFlagOptimiseMask){
		  // Use the mask optimisation routine provided by WALLABY
		  this->itsCube.calcObjectWCSparams();
		  OptimisedGrower grower(this->itsParset.makeSubset("optimiseMask."));
		  ASKAPLOG_DEBUG_STR(logger, "Defining the optimised grower");
		  grower.define(&this->itsCube);
		  ASKAPLOG_DEBUG_STR(logger, "Optimising the mask for all " << this->itsCube.getNumObj()<<" objects");
		  double x,y,z;
		  for(size_t o=0;o<this->itsCube.getNumObj();o++){
		    Detection *det=this->itsCube.pObject(o);
		    ASKAPLOG_DEBUG_STR(logger, "Object #"<<o<<", at (RA,DEC)=("<<det->getRA()<<","<<det->getDec()
				       <<") and velocity=" << det->getVel() << ". W50 = " << det->getW50()
				       << " so the spectral range is from "<<this->itsCube.header().velToSpec(det->getV50Min())
				       <<" to " << this->itsCube.header().velToSpec(det->getV50Max())); 
		    this->itsCube.header().wcsToPix(det->getRA(),det->getDec(),this->itsCube.header().velToSpec(det->getVel()+det->getW50()),x,y,z);
		    int zmax=std::max(0,std::min(int(this->itsCube.getDimZ()-1),int(z)));
		    this->itsCube.header().wcsToPix(det->getRA(),det->getDec(),this->itsCube.header().velToSpec(det->getVel()-det->getW50()),x,y,z);
		    int zmin=std::min(int(this->itsCube.getDimZ()-1),std::max(0,int(z)));
		    if(zmin>zmax) std::swap(zmin,zmax);
		    grower.setMaxMinZ(zmax,zmin);
		    ASKAPLOG_DEBUG_STR(logger, "Central pixel ("<<det->getXcentre() <<","<<det->getYcentre()<<","<<det->getZcentre()
				       <<") with " << det->getSize() <<" pixels, filling z range " << zmin << " to " << zmax);
		    grower.grow(det);
		    ASKAPLOG_DEBUG_STR(logger, "Now has central pixel ("<<det->getXcentre() <<","<<det->getYcentre()<<","<<det->getZcentre()
				       <<") with " << det->getSize() <<" pixels");
		  }
		  ASKAPLOG_DEBUG_STR(logger, "Updating the detection map");
		  grower.updateDetectMap(this->itsCube.getDetectMap());
		  ASKAPLOG_DEBUG_STR(logger,"Merging objects" );
		  bool growthflag=this->itsCube.pars().getFlagGrowth();
		  this->itsCube.pars().setFlagGrowth(false); // don't do any further growing in the second lot of merging
		  this->itsCube.ObjectMerger(); // do a second merging to clean up any objects that have joined together.
		  this->itsCube.pars().setFlagGrowth(growthflag);
		  ASKAPLOG_DEBUG_STR(logger, "Finished mask optimisation");
		}


                if (itsComms.isParallel()) {
                    this->itsCube.pars().setMinPix(minpix);
                    this->itsCube.pars().setMinChannels(minchan);
		    this->itsCube.pars().setMinVoxels(minvox);
                }

		this->finaliseDetection();
            }
	}

      void DuchampParallel::finaliseDetection()
      {

	  // Remove non-edge sources that are smaller than originally requested, as these won't be grown any further.
	  std::vector<duchamp::Detection> edgelist,goodlist;
	  for(size_t i=0; i<this->itsCube.getNumObj();i++){
	      sourcefitting::RadioSource src(this->itsCube.getObject(i));
	      src.setAtEdge(this->itsCube, this->itsSubimageDef, itsComms.rank() - 1);
	      if(src.isAtEdge()) edgelist.push_back(this->itsCube.getObject(i));
	      else goodlist.push_back(this->itsCube.getObject(i));
	  }
	  duchamp::finaliseList(goodlist,this->itsCube.pars());
	  size_t ngood=goodlist.size(),nedge=edgelist.size();
	  this->itsCube.clearDetectionList();
	  for(size_t i=0;i<edgelist.size();i++) goodlist.push_back(edgelist[i]);
	  this->itsCube.ObjectList() = goodlist;
	  //-------

	  ASKAPLOG_DEBUG_STR(logger, this->workerPrefix() << "Calculating WCS params");
	  this->itsCube.calcObjectWCSparams();
	  if(this->itsFlagVariableThreshold){
	      // Need to set the peak SNR for each object
	      for(size_t i=0; i<this->itsCube.getNumObj();i++){
		  std::vector<Voxel> voxlist = this->itsCube.getObject(i).getPixelSet();
		  for(size_t v=0;v<voxlist.size();v++){
		      float snr=this->itsCube.getReconValue(voxlist[v].getX(),voxlist[v].getY(),voxlist[v].getZ());
		      if(v==0 || snr>this->itsCube.getObject(i).getPeakSNR()) this->itsCube.pObject(i)->setPeakSNR(snr);
		  }
	      }
	  }
	  ASKAPLOG_INFO_STR(logger,  this->workerPrefix() << "Found " << this->itsCube.getNumObj() << " objects, of which "
			    << nedge << " are on the boundary and " << ngood << " are good.");
	
      }
 
        //**************************************************************//

        void DuchampParallel::fitSources()
        {
            /// @details The list of RadioSource objects is populated: one
            /// for each of the detected objects. If the 2D profile fitting
            /// is requested, all sources that are not on the image boundary
            /// are fitted by the RadioSource::fitGauss(float *, long *)
            /// function. The fitting for those on the boundary is left for
            /// the master to do after they have been combined with objects
            /// from other subimages.
            ///
            /// @todo Make the boundary determination smart enough to know
            /// which side is adjacent to another subimage.
            if (itsComms.isWorker()) {
                // don't do fit if we have a spectral axis.
                bool flagIs2D = !this->itsCube.header().canUseThirdAxis() || this->is2D();
		this->itsFitParams.setFlagDoFit(this->itsFitParams.doFit() && flagIs2D);

                if (this->itsFitParams.doFit())
		  ASKAPLOG_INFO_STR(logger, this->workerPrefix() << "Fitting source profiles.");

                for (size_t i = 0; i < this->itsCube.getNumObj(); i++) {
		  if (this->itsFitParams.doFit())
                        ASKAPLOG_INFO_STR(logger, this->workerPrefix() << "Setting up source #" << i + 1 << " / " << this->itsCube.getNumObj() 
					  << ", size " << this->itsCube.getObject(i).getSize() 
					  << ", peaking at (x,y)=("<<this->itsCube.getObject(i).getXPeak()+this->itsCube.getObject(i).getXOffset()
					  << "," << this->itsCube.getObject(i).getYPeak()+this->itsCube.getObject(i).getYOffset() << ")");

                    sourcefitting::RadioSource src(this->itsCube.getObject(i));
		    src.defineBox(this->itsCube.pars().section(), this->itsFitParams, this->itsCube.header().getWCS()->spec);
		    src.setFitParams(this->itsFitParams);
		    src.setDetectionThreshold(this->itsCube, this->itsFlagVariableThreshold, this->itsVarThresher->snrImage());
		    src.prepareForFit(this->itsCube,true);
		    // Only do fit if object is not next to boundary
		    src.setAtEdge(this->itsCube, this->itsSubimageDef, itsComms.rank() - 1);
		    
		    if (itsComms.nProcs() == 1) src.setAtEdge(false);

		    if (!src.isAtEdge() && this->itsFitParams.doFit())
		      this->fitSource(src);

                    this->itsSourceList.push_back(src);
                }
            }
        }

       //**************************************************************//

      void DuchampParallel::fitSource(sourcefitting::RadioSource &src)
      {

	  if (this->itsFitParams.fitJustDetection()) {
	      ASKAPLOG_DEBUG_STR(logger, this->workerPrefix() << "Fitting to detected pixels");
	      std::vector<PixelInfo::Voxel> voxlist = src.getPixelSet(this->itsCube.getArray(), this->itsCube.getDimArray());
	      src.fitGaussNew(&voxlist, this->itsFitParams);
	  } else {
	      src.fitGauss(this->itsCube.getArray(), this->itsCube.getDimArray(), this->itsFitParams);
	  }

	  for(int t=1;t<=2;t++)
	      src.findSpectralTerm(this->itsSpectralTermImages[t-1], t, this->itsFlagFindSpectralTerms[t-1]);

      }


        //**************************************************************//

        void DuchampParallel::sendObjects()
        {
            /// @details The RadioSource objects on each worker, which
            /// contain each detected object, are sent to the Master node
            /// via LOFAR Blobs.
            ///
            /// In the non-parallel case, we put together a voxel list. Not
            /// sure whether this is necessary at this point.
            /// @todo Sort out voxelList necessity.
            if (itsComms.isWorker()) {
                int32 num = this->itsCube.getNumObj();
                int16 rank = itsComms.rank();

                if (itsComms.isParallel()) {
                    LOFAR::BlobString bs;
                    bs.resize(0);
                    LOFAR::BlobOBufString bob(bs);
                    LOFAR::BlobOStream out(bob);
                    out.putStart("detW2M", 1);
                    out << rank << num;
                    // send the start positions of the subimage
                    out << this->itsCube.pars().section().getStart(0)
                        << this->itsCube.pars().section().getStart(1)
                        << this->itsCube.pars().section().getStart(this->itsCube.header().getWCS()->spec);
                    std::vector<sourcefitting::RadioSource>::iterator src = this->itsSourceList.begin();

                    for (; src < this->itsSourceList.end(); src++) {
                        // for each RadioSource object, send to master
                        out << *src;

                    }

                    out.putEnd();
                    itsComms.sendBlob(bs, 0);
                    ASKAPLOG_INFO_STR(logger, this->workerPrefix() << "Sent detection list to the master");
                } else {
                }
            }
        }

        //**************************************************************//

      void DuchampParallel::receiveObjects()
      {
	/// @details On the Master node, receive the list of RadioSource
	/// objects sent by the workers. Also receives the list of
	/// detected and surrounding voxels - these will be used to
	/// calculate parameters of any merged boundary sources.
	/// @todo Voxellist is really only needed for the boundary sources.
	if (!itsComms.isParallel() || itsComms.isMaster()) {
	  ASKAPLOG_INFO_STR(logger,  this->workerPrefix() << "Retrieving lists from workers");

	  if (itsComms.isParallel()) {
	    LOFAR::BlobString bs;
	    int16 rank;
	    int32 numObj;

	    // don't do fit if we have a spectral axis.
	    bool flagIs2D = !this->itsCube.header().canUseThirdAxis() || this->is2D();
	    this->itsFitParams.setFlagDoFit(this->itsFitParams.doFit() && flagIs2D);
		
	    // list of fit types, for use in correcting positions of fitted components
	    std::vector<std::string>::iterator fittype;
	    std::vector<std::string> fittypelist = sourcefitting::availableFitTypes;
	    fittypelist.push_back("best");
	    fittypelist.push_back("guess");

	    // ASKAPLOG_DEBUG_STR(logger, this->workerPrefix() << "Offsets for master : " << this->itsCube.pars().getXOffset() 
	    // 		       << " " << this->itsCube.pars().getYOffset() << " " << this->itsCube.pars().getZOffset());

	    for (int i = 1; i < itsComms.nProcs(); i++) {
	      ASKAPLOG_DEBUG_STR(logger, this->workerPrefix() << "In loop #"<<i<<" of reading from workers");
	      itsComms.receiveBlob(bs, i);
	      LOFAR::BlobIBufString bib(bs);
	      LOFAR::BlobIStream in(bib);
	      int version = in.getStart("detW2M");
	      ASKAPASSERT(version == 1);
	      in >> rank >> numObj;
	      ASKAPLOG_INFO_STR(logger, this->workerPrefix() << "Starting to read "
				<< numObj << " objects from worker #" << rank);
	      int xstart, ystart, zstart;
	      in >> xstart >> ystart >> zstart;
	      // ASKAPLOG_DEBUG_STR(logger, this->workerPrefix() << "Offsets for worker #"<<rank<<": " << xstart << " " << ystart << " " << zstart);

	      for (int obj = 0; obj < numObj; obj++) {
		sourcefitting::RadioSource src;
		in >> src;
		// Correct for any offsets.
		// If the full cube is a subsection of a larger one, then we need to correct for what the master offsets are.
		src.setXOffset(xstart - this->itsCube.pars().getXOffset());
		src.setYOffset(ystart - this->itsCube.pars().getYOffset());
		src.setZOffset(zstart - this->itsCube.pars().getZOffset());
		// ASKAPLOG_DEBUG_STR(logger, this->workerPrefix() << "Source " << src.getID() << " with edgeflag="<<src.isAtEdge()<<" has offsets on the master of " <<
		// 		   src.getXOffset() << " " << src.getYOffset() << " " << src.getZOffset());
		src.addOffsets();
		src.calcParams();
		src.calcWCSparams(this->itsCube.header());

		// And now set offsets to those of the full image as we are in the master cube
		src.setOffsets(this->itsCube.pars());
		src.defineBox(this->itsCube.pars().section(), this->itsFitParams, this->itsCube.header().getWCS()->spec);
		src.fitparams() = this->itsFitParams;
		if(src.isAtEdge()) this->itsEdgeSourceList.push_back(src);
		else{
                    src.setHeader(this->itsCube.pHeader());
		    if (src.hasEnoughChannels(this->itsCube.pars().getMinChannels())  
			&& (src.getSpatialSize() >= this->itsCube.pars().getMinPix()) ) {
			// Only add the source if it meets the true criteria for size
			this->itsSourceList.push_back(src);
		    }
		}

	      }
	      ASKAPLOG_INFO_STR(logger, this->workerPrefix() << "Received list of size " << numObj << " from worker #" << rank);
	      ASKAPLOG_INFO_STR(logger, this->workerPrefix() << "Now have " << this->itsSourceList.size() << " good objects and " << this->itsEdgeSourceList.size() << " edge objects");
	      in.getEnd();
	    }
	  }

	}

      }

        //**************************************************************//

        void DuchampParallel::cleanup()
        {
            /// @details Done on the Master node. This function gathers the
            /// sources that are marked as on the boundary of subimages, and
            /// combines them via the duchamp::Cubes::ObjectMerger()
            /// function. The resulting sources are then fitted (if so
            /// required) and have their WCS parameters calculated by the
            /// ObjectParameteriser class
            ///
            /// Once this is done, these sources are added to the cube
            /// detection list, along with the non-boundary objects. The
            /// final list of RadioSource objects is then sorted (by the
            /// Name field) and given object IDs.

	  if(itsComms.isParallel() && itsComms.isWorker()){
	      // need to call ObjectParameteriser only, so that the distributed calculation works

	      ASKAPLOG_DEBUG_STR(logger, this->workerPrefix() << "Parameterising edge objects in distributed manner");
	      ObjectParameteriser objParam(this->itsComms);
	      objParam.initialise(this);
	      objParam.distribute();
	      objParam.parameterise();
	      objParam.gather();

	  }


            if (!itsComms.isParallel() || itsComms.isMaster()) {
                ASKAPLOG_INFO_STR(logger, this->workerPrefix() << "Beginning the cleanup");

                std::vector<sourcefitting::RadioSource>::iterator src;

		ASKAPLOG_INFO_STR(logger, this->workerPrefix() << "num edge sources in cube = " << this->itsEdgeSourceList.size());

		this->itsCube.clearDetectionList();

                if (this->itsEdgeSourceList.size() > 0) { // if there are edge sources
                    for (src = this->itsEdgeSourceList.begin(); src < this->itsEdgeSourceList.end(); src++) this->itsCube.addObject(*src);
                    ASKAPLOG_INFO_STR(logger, this->workerPrefix() << "num edge sources in cube = " << this->itsCube.getNumObj());
                    bool growthflag = this->itsCube.pars().getFlagGrowth();
                    this->itsCube.pars().setFlagGrowth(false);  // can't grow as don't have flux array in itsCube
		    ///@todo Need to grow edge sources before sending to master, which means finding objects at edge above growth threshold but below detection threshold
                    ASKAPLOG_INFO_STR(logger, this->workerPrefix() << "Merging edge sources");
                    this->itsCube.ObjectMerger();
                    ASKAPLOG_INFO_STR(logger, this->workerPrefix() << "num edge sources in cube after merging = " << this->itsCube.getNumObj());
                    this->itsCube.pars().setFlagGrowth(growthflag);

		    this->itsEdgeSourceList.clear();
                    for (size_t i = 0; i < this->itsCube.getNumObj(); i++) {
                        sourcefitting::RadioSource src(this->itsCube.getObject(i));

			src.defineBox(this->itsCube.pars().section(), this->itsFitParams, this->itsCube.header().getWCS()->spec);

                        this->itsEdgeSourceList.push_back(src);
                    }

                }

		ObjectParameteriser objParam(this->itsComms);
		objParam.initialise(this);
		objParam.distribute();
		objParam.parameterise();
		objParam.gather();

		ASKAPLOG_INFO_STR(logger, this->workerPrefix() << "Finished parameterising " << this->itsEdgeSourceList.size() <<" edge sources");

		for(src=this->itsEdgeSourceList.begin(); src<this->itsEdgeSourceList.end();src++){
		    ASKAPLOG_DEBUG_STR(logger, "'Edge' source, name " << src->getName());
		    this->itsSourceList.push_back(*src);
		}
		this->itsEdgeSourceList.clear();

                ASKAPLOG_INFO_STR(logger, this->workerPrefix() << "Now have a total of " << this->itsSourceList.size() << " sources.");

		SortDetections(this->itsSourceList, this->itsCube.pars().getSortingParam());

                this->itsCube.clearDetectionList();

                for (src = this->itsSourceList.begin(); src < this->itsSourceList.end(); src++) {
                    src->setID(src - this->itsSourceList.begin() + 1);
                    src->setAtEdge(this->itsCube, this->itsSubimageDef, itsComms.rank() - 1);

                    if (src->isAtEdge()) src->addToFlagText("E");
                    else src->addToFlagText("-");

                    this->itsCube.addObject(duchamp::Detection(*src));
                }

                ASKAPLOG_INFO_STR(logger, this->workerPrefix() << "Finished adding sources to cube. Now have " << this->itsCube.getNumObj() << " objects.");

            }

        }

        //**************************************************************//

        void DuchampParallel::printResults()
        {
            /// @details The final list of detected objects is written to
            /// the terminal and to the results file in the standard Duchamp
            /// manner.
            if (itsComms.isMaster()) {

		this->itsCube.sortDetections();

                std::vector<std::string> outtypes = this->itsFitParams.fitTypes();
                outtypes.push_back("best");
	      
		if(this->itsCube.pars().getFlagNegative()){
		  this->itsCube.invert(false,true);

		  std::vector<sourcefitting::RadioSource>::iterator src;
		  for (src = this->itsSourceList.begin(); src < this->itsSourceList.end(); src++) {
		    for (size_t t = 0; t < outtypes.size(); t++) {
		      for(size_t i=0;i<src->numFits(outtypes[t]);i++){
			Double f = src->fitset(outtypes[t])[i].flux();
			src->fitset(outtypes[t])[i].setFlux(f * -1);
		      }
		    }
		  }

		}
		ASKAPLOG_INFO_STR(logger, this->workerPrefix() << "Found " << this->itsCube.getNumObj() << " sources.");

		ASKAPLOG_INFO_STR(logger, "Writing to output catalogue " << this->itsCube.pars().getOutFile());
                this->itsCube.outputCatalogue();

                if (this->itsCube.pars().getFlagLog() && (this->itsCube.getNumObj() > 0)) {
		  this->itsCube.logSummary();
                }

		this->itsCube.outputAnnotations();

		if(this->itsCube.pars().getFlagVOT()){
		    ASKAPLOG_INFO_STR(logger, "Writing to output VOTable " << this->itsCube.pars().getVOTFile());
		  this->itsCube.outputDetectionsVOTable();
		}

		if(this->itsCube.pars().getFlagTextSpectra()){
		    ASKAPLOG_INFO_STR(logger,"Saving spectra to text file " << this->itsCube.pars().getSpectraTextFile());
		    this->itsCube.writeSpectralData();
		}

		
		if(this->itsCube.pars().getFlagWriteBinaryCatalogue() && (this->itsCube.getNumObj()>0)){
		    ASKAPLOG_INFO_STR(logger, "Creating binary catalogue of detections, called " << this->itsCube.pars().getBinaryCatalogue());
		    this->itsCube.writeBinaryCatalogue();
		}


		if(this->itsFitParams.doFit()){

		  for (size_t t = 0; t < outtypes.size(); t++) {
 		    
		      duchamp::Catalogues::CatalogueSpecification columns = sourcefitting::fullCatalogue(this->itsCube.getFullCols(), this->itsCube.header());
		      setupCols(columns,this->itsSourceList,outtypes[t]);

		    std::string filename=sourcefitting::convertSummaryFile(this->itsFitSummaryFile.c_str(), outtypes[t]);
		    AskapAsciiCatalogueWriter writer(filename);
		    ASKAPLOG_DEBUG_STR(logger, "Writing Fit results to " << filename);
		    writer.setup(this);
		    writer.setFitType(outtypes[t]);
		    writer.setColumnSpec(&columns);
		    writer.setSourceList(&this->itsSourceList);
		    writer.openCatalogue();
		    writer.writeTableHeader();
		    writer.writeEntries();
		    writer.writeFooter();
		    writer.closeCatalogue();
		    
		    filename = filename.replace(filename.rfind(".txt"), 4, ".xml");
		    AskapVOTableCatalogueWriter vowriter(filename);
		    ASKAPLOG_DEBUG_STR(logger, "Writing Fit results to the VOTable " << filename);
		    vowriter.setup(this);
		    vowriter.setFitType(outtypes[t]);
		    vowriter.setColumnSpec(&columns);
		    vowriter.setSourceList(&this->itsSourceList);
		    vowriter.openCatalogue();
		    vowriter.writeHeader();
		    vowriter.writeParameters();
		    if(this->is2D()){
			double ra,dec,freq;
			this->itsCube.header().pixToWCS(this->itsCube.getDimX()/2.,this->itsCube.getDimY()/2.,0.,ra,dec,freq);
			std::string frequnits(this->itsCube.header().WCS().cunit[this->itsCube.header().WCS().spec]);
			vowriter.writeParameter(duchamp::VOParam("Reference frequency","em.freq;meta.main","float",freq,0,frequnits));
		    }
		    vowriter.writeStats();
		    vowriter.writeTableHeader();
		    vowriter.writeEntries();
		    vowriter.writeFooter();
		    vowriter.closeCatalogue();
		  
		    
		    filename = this->itsParset.getString("outputComponentParset","");
		    if(filename!=""){
			AskapComponentParsetWriter pwriter(filename);
			ASKAPLOG_INFO_STR(logger, "Writing Fit results to parset named " << filename);
			pwriter.setup(this);
			pwriter.setFitType("best");
			pwriter.setSourceList(&this->itsSourceList);
			pwriter.setFlagReportSize(this->itsParset.getBool("outputComponentParset.reportSize",true));
			pwriter.setMaxNumComponents(this->itsParset.getInt("outputComponentParset.maxNumComponents",-1));
			pwriter.openCatalogue();
			pwriter.writeTableHeader();
			pwriter.writeEntries();
			pwriter.writeFooter();
			pwriter.closeCatalogue();
		    }

  
		  }
		  

		  if (this->itsFitParams.doFit()) this->writeFitAnnotations();

		}

            } else {
            }
        }

        //**************************************************************//

	void DuchampParallel::extract()
	{

	    for(size_t i=0;i<this->itsSourceList.size();i++){
		// make sure the boxes are defined for each of the sources prior to distribution
		this->itsSourceList[i].defineBox(this->itsCube.pars().section(), this->itsFitParams, this->itsCube.header().getWCS()->spec);
	    }

	    ExtractionFactory extractor(this->itsComms, this->itsParset);
	    extractor.setParams(this->itsCube.pars());
	    extractor.setSourceList(this->itsSourceList);
	    extractor.distribute();
	    extractor.extract();
	
	}

	void DuchampParallel::writeToFITS()
	{
	  if(!this->itsIsFITSFile){
	    if(this->itsComms.isMaster())
	      ASKAPLOG_WARN_STR(logger, "Writing the Duchamp-style FITS arrays currently requires the input file to be FITS, which is not the case here.");
	  }
	  else if(!this->itsComms.isParallel()){
	      this->itsCube.pars().setFlagBlankPix(false);
	      this->itsCube.writeToFITS(); 
	  }

	}


	void DuchampParallel::writeFitAnnotations()
	{
	    /// @details This function writes an annotation file
            /// showing the location and shape of the fitted 2D
            /// Gaussian components. It makes use of the
            /// RadioSource::writeFitToAnnotationFile() function. The
            /// file written to is given by the input parameter
            /// fitAnnotationFile.

	    bool doBoxAnnot = !this->itsFitParams.fitJustDetection() && (this->itsFitAnnotationFile != this->itsFitBoxAnnotationFile);

	    if(this->itsSourceList.size() > 0) {

		for(int i=0;i<3;i++){
		    AnnotationWriter *writerFit=0;
		    AnnotationWriter *writerBox=0;
		    switch(i){
		    case 0: //Karma
			if(this->itsCube.pars().getFlagKarma()){
			    writerFit = new KarmaAnnotationWriter(this->itsFitAnnotationFile);
			    ASKAPLOG_INFO_STR(logger, "Writing fit results to karma annotation file: " << this->itsFitAnnotationFile << " with address of writer = " << writerFit);
			    if(doBoxAnnot)
				writerBox = new KarmaAnnotationWriter(this->itsFitBoxAnnotationFile);
			    break;
			case 1://DS9
			    if(this->itsCube.pars().getFlagDS9()){
				std::string filename=itsFitAnnotationFile;
				size_t loc=filename.rfind(".ann");
				if(loc==std::string::npos) filename += ".reg";
				else filename.replace(loc,4,".reg");
				writerFit = new DS9AnnotationWriter(filename);
				ASKAPLOG_INFO_STR(logger, "Writing fit results to DS9 annotation file: " << filename << " with address of writer = " << writerFit);
				if(doBoxAnnot){
				    filename = this->itsFitBoxAnnotationFile;
				    size_t loc=filename.rfind(".ann");
				    if(loc==std::string::npos) filename += ".reg";
				    else filename.replace(loc,4,".reg");
				    writerBox = new DS9AnnotationWriter(filename);
				}
			    }
			    break;
			case 2://CASA
			    if(this->itsCube.pars().getFlagCasa()){
				std::string filename=itsFitAnnotationFile;
				size_t loc=filename.rfind(".ann");
				if(loc==std::string::npos) filename += ".crf";
				else filename.replace(loc,4,".crf");
				writerFit = new CasaAnnotationWriter(filename);
				ASKAPLOG_INFO_STR(logger, "Writing fit results to casa annotation file: " << filename << " with address of writer = " << writerFit);
				if(doBoxAnnot){
				    filename = this->itsFitBoxAnnotationFile;
				    size_t loc=filename.rfind(".ann");
				    if(loc==std::string::npos) filename += ".reg";
				    else filename.replace(loc,4,".reg");
				    writerBox = new DS9AnnotationWriter(filename);
				}
			    }
			    break;	
			}
		    }
			
		    if(writerFit!=0){
			writerFit->setup(&this->itsCube);
			writerFit->openCatalogue();
			writerFit->setColourString("BLUE");
			writerFit->writeHeader();
			writerFit->writeParameters();
			writerFit->writeStats();
			writerFit->writeTableHeader();
			// writer->writeEntries();

			if(writerBox != 0){
			    writerBox->setup(&this->itsCube);
			    writerBox->openCatalogue();
			    writerFit->setColourString("BLUE");
			    writerBox->writeHeader();
			    writerBox->writeParameters();
			    writerBox->writeStats();
			    writerBox->writeTableHeader();
			}

			std::vector<sourcefitting::RadioSource>::iterator src;
			int num=1;
			for (src = this->itsSourceList.begin(); src < this->itsSourceList.end(); src++) {
			    src->writeFitToAnnotationFile(writerFit, num, true, this->itsFitAnnotationFile == this->itsFitBoxAnnotationFile);
			    if(doBoxAnnot && writerBox!=0) src->writeFitToAnnotationFile(writerBox, num, false, true);
			    num++;
			}

			writerFit->writeFooter();
			writerFit->closeCatalogue();
			if(writerBox!=0){
			    writerBox->writeFooter();
			    writerBox->closeCatalogue();
			}
		    }

		    if(writerFit!=0) delete writerFit;
		    if(writerBox!=0) delete writerBox;
		}
		
	    }

	}


        //**************************************************************//

      void DuchampParallel::gatherStats()
      {
	/// @details A front-end function that calls all the statistics
	/// functions. Net effect is to find the mean/median and
	/// stddev/MADFM for the entire dataset and store these values in
	/// the master's itsCube statsContainer.
	if (this->itsFlagVariableThreshold){
	  if(this->itsCube.pars().getFlagUserThreshold())
	    ASKAPLOG_WARN_STR(logger, "Since a variable threshold has been requested, the threshold given ("
			      <<this->itsCube.pars().getThreshold()<<") is changed to a S/N-based one of "<<this->itsCube.pars().getCut()<<" sigma");
	  ASKAPLOG_DEBUG_STR(logger, this->workerPrefix() << "Setting user threshold to " << this->itsCube.pars().getCut());
	  this->itsCube.pars().setThreshold(this->itsCube.pars().getCut());
	  this->itsCube.pars().setFlagUserThreshold(true);
	  if(this->itsCube.pars().getFlagGrowth()){
	    ASKAPLOG_DEBUG_STR(logger, this->workerPrefix() << "Setting user growth threshold to " << this->itsCube.pars().getGrowthCut());
	    this->itsCube.pars().setGrowthThreshold(this->itsCube.pars().getGrowthCut());
	    this->itsCube.pars().setFlagUserGrowthThreshold(true);
	  }
	  this->itsCube.stats().setThreshold(this->itsCube.pars().getCut());
	  
	}
	else if(!this->itsComms.isParallel() || this->itsFlagThresholdPerWorker){
	  if(this->itsComms.isWorker()){
	    if(this->itsComms.isParallel())
	      ASKAPLOG_DEBUG_STR(logger, this->workerPrefix() << "Calculating stats for each worker individually");
	    else
	      ASKAPLOG_DEBUG_STR(logger, this->workerPrefix() << "Calculating stats");
	    this->itsCube.setCubeStats();
	    ASKAPLOG_INFO_STR(logger, this->workerPrefix() << "Stats are as follows:");
	    std::cout << this->itsCube.stats();
	  }
	  if(this->itsComms.isParallel() && this->itsComms.isMaster()){
	    this->itsCube.stats().setThreshold(this->itsCube.pars().getCut());
	    this->itsCube.pars().setThreshold(this->itsCube.pars().getCut());
	  }
	  else                          this->itsCube.pars().setThreshold(this->itsCube.stats().getThreshold());
	  this->itsCube.pars().setFlagUserThreshold(true);
	  ASKAPLOG_INFO_STR(logger, this->workerPrefix() << "Threshold = " << this->itsCube.stats().getThreshold());
	}    
	else if (!this->itsFlagVariableThreshold &&
		 (!this->itsCube.pars().getFlagUserThreshold() ||
		  (this->itsCube.pars().getFlagGrowth() && !this->itsCube.pars().getFlagUserGrowthThreshold()))) {

	    ParallelStats parstats(this->itsComms, &this->itsCube);
	    parstats.findDistributedStats();

	} else{
	  this->itsCube.stats().setThreshold(this->itsCube.pars().getThreshold());
	}
      }

        //**************************************************************//

        void DuchampParallel::setThreshold()
        {
            /// @details The detection threshold value (which has been
            /// already calculated) is properly set for use. In the
            /// distributed case, this means the master sends it to
            /// the workers via LOFAR Blobs, and the workers
            /// individually set it.

	    if(!this->itsFlagThresholdPerWorker){
		// when doing a threshold per worker, have already set the threshold.

		double threshold, mean, stddev;
		if( this->itsComms.isParallel()){
		    if(this->itsComms.isMaster()){
			LOFAR::BlobString bs;
			bs.resize(0);
			LOFAR::BlobOBufString bob(bs);
			LOFAR::BlobOStream out(bob);
			out.putStart("threshM2W", 1);
			threshold = this->itsCube.stats().getThreshold();
			mean = this->itsCube.stats().getMiddle();
			stddev = this->itsCube.stats().getSpread();
			out << threshold << mean << stddev;
			out.putEnd();
			itsComms.broadcastBlob(bs, 0);
			ASKAPLOG_INFO_STR(logger, this->workerPrefix() << "Threshold = " << this->itsCube.stats().getThreshold());
		    } 
		    else if(this->itsComms.isWorker()) {
			LOFAR::BlobString bs;
			itsComms.broadcastBlob(bs, 0);
			LOFAR::BlobIBufString bib(bs);
			LOFAR::BlobIStream in(bib);
			int version = in.getStart("threshM2W");
			ASKAPASSERT(version == 1);
			in >> threshold >> mean >> stddev;
			in.getEnd();
			this->itsCube.stats().setRobust(false);
			this->itsCube.stats().setMean(mean);
			this->itsCube.stats().setStddev(stddev);
			this->itsCube.stats().define(this->itsCube.stats().getMiddle(),0.F,this->itsCube.stats().getSpread(),1.F);
		    
			if (!this->itsCube.pars().getFlagUserThreshold()) {
			    this->itsCube.stats().setThresholdSNR(this->itsCube.pars().getCut());
			    this->itsCube.pars().setFlagUserThreshold(true);
			    this->itsCube.pars().setThreshold(this->itsCube.stats().getThreshold());
			}
		    }
		    else ASKAPTHROW(AskapError, "Neither Master nor Worker!");
		}
		else {
		    // serial case
                    if (this->itsCube.pars().getFlagUserThreshold())
                        threshold = this->itsCube.pars().getThreshold();
                    else
                        threshold = this->itsCube.stats().getMiddle() + this->itsCube.stats().getSpread() * this->itsCube.pars().getCut();
		}
                ASKAPLOG_INFO_STR(logger, this->workerPrefix() << "Setting threshold to be " << threshold);
                this->itsCube.pars().setThreshold(threshold);
	    }
        }


      //**************************************************************//

 	
      duchamp::OUTCOME DuchampParallel::getCASA(DATATYPE typeOfData, bool useSubimageInfo)
      {

	/// @details This is the front-end to the image-access
	/// functionality for CASA images. It replicates (kinda) the
	/// behaviour of duchamp::Cube::getCube(). First the image is
	/// opened, then we get the metadata for the image via
	/// getCasaMetadata. Then the subimage that we want is defined
	/// (including the parsing of any subsections given in the
	/// parset), then, if we request IMAGE data, the actual pixel
	/// values are read from the image and stored in the itsCube
	/// object.
	/// @param typeOfData What sort of data are we after? Image
	/// data or just Metadata?
	/// @param useSubimageInfo Whether to use the information of
	/// the distributed nature of the data to determine the
	/// subimage shape, or whether just to get the whole image
	/// dimensions.
	/// @return duchamp::SUCCESS if successful, duchamp::FAILURE otherwise.

	ImageOpener::registerOpenImageFunction(ImageOpener::FITS, FITSImage::openFITSImage);
	ImageOpener::registerOpenImageFunction(ImageOpener::MIRIAD, MIRIADImage::openMIRIADImage);
	const LatticeBase* lattPtr = ImageOpener::openImage(this->itsCube.pars().getImageFile());
	if (lattPtr == 0)
	  ASKAPTHROW(AskapError, "Requested image \"" << this->itsCube.pars().getImageFile() << "\" does not exist or could not be opened.");
	const ImageInterface<Float>* imagePtr = dynamic_cast<const ImageInterface<Float>*>(lattPtr);


	// Define the subimage - need to be done before metadata, as the latter needs the subsection & offsets
	const SubImage<Float> *sub = this->getSubimage(imagePtr, useSubimageInfo);

	if(this->getCasaMetadata(sub, typeOfData) == duchamp::FAILURE) return duchamp::FAILURE;

	ASKAPLOG_DEBUG_STR(logger, this->workerPrefix() << "Have subimage with shape " << sub->shape() << " and subsection " << this->itsCube.pars().section().getSection());

	if(typeOfData == IMAGE){
	  
	  ASKAPLOG_INFO_STR(logger, "Reading data from image " << this->itsCube.pars().getImageFile());

	  casa::Array<Float> subarray(sub->shape());	  
	  const casa::MaskedArray<Float> *msub = new casa::MaskedArray<Float>(sub->get(),sub->getMask());
	  float minval = min(*msub)-10.;
	  subarray = *msub;
	  delete msub;
	  if(sub->hasPixelMask()){
	      subarray(!sub->getMask()) = minval;
	      this->itsCube.pars().setBlankPixVal(minval);
	      this->itsCube.pars().setBlankKeyword(0);
	      this->itsCube.pars().setBscaleKeyword(1.);
	      this->itsCube.pars().setBzeroKeyword(minval);
	      this->itsCube.pars().setFlagBlankPix(true);
	  }

	  size_t *dim = getDim(sub);
// 	  std::cout << this->itsCube.pars()<<"\n";
	  // A HACK TO ENSURE THE RECON ARRAY IS ALLOCATED IN THE CASE OF VARIABLE THRESHOLD OR WEIGHTS IMAGE SCALING
	  bool flag=this->itsCube.pars().getFlagATrous();
	  if(this->itsFlagVariableThreshold || this->itsWeighter->doScaling()) this->itsCube.pars().setFlagATrous(true);
	  this->itsCube.initialiseCube(dim);
	  if(this->itsFlagVariableThreshold || this->itsWeighter->doScaling()) this->itsCube.pars().setFlagATrous(flag);
	  if(this->itsCube.getDimZ()==1){
	    this->itsCube.pars().setMinChannels(0);
	  }
	  this->itsCube.saveArray(subarray.data(), subarray.size());
	  
	  
	}

	return duchamp::SUCCESS;

      }

      //**************************************************************//

      const SubImage<Float>* DuchampParallel::getSubimage(const ImageInterface<Float>* imagePtr, bool useSubimageInfo)
      {

	/// @details Define the shape/size of the subimage being used,
	/// and return a pointer that can be used to extract the image
	/// data. The subimage is defined by the itsSubimageDef
	/// object, which, when useSubimageInfo=true, takes into
	/// account how the image is distributed amongst workers. If
	/// useSubimageInfo=false, the whole image is considered. The
	/// image subsection and the statistics subsection are both
	/// parsed and tested for validity.
	/// @param imagePtr Pointer to the image - needs to be opened
	/// and valid
	/// @param useSubimageInfo Whether to use infomation about the
	/// distribution of the image amongst workers.
	/// @return A casa::SubImage pointer to the desired sub-image.

	wcsprm *wcs = casaImageToWCS(imagePtr);
	this->itsSubimageDef.define(wcs);
	this->itsSubimageDef.setImage(this->itsCube.pars().getImageFile());
	this->itsSubimageDef.setInputSubsection(this->itsBaseSubsection);
	size_t *dim = getDim(imagePtr);
	reportDim(dim,imagePtr->ndim());
	this->itsSubimageDef.setImageDim(dim, imagePtr->ndim());
	
	if(useSubimageInfo && (!this->itsComms.isParallel() || this->itsComms.isWorker())){
	    this->itsCube.pars().section() = this->itsSubimageDef.section(this->itsComms.rank()-1);
	}
	else if (!this->itsCube.pars().getFlagSubsection() || this->itsCube.pars().getSubsection() == "") {
	  this->itsCube.pars().setSubsection(nullSection(this->itsSubimageDef.getImageDim().size()));
	}
	this->itsCube.pars().setFlagSubsection(true);

	// Now parse the sections to get them properly set up
	if(this->itsCube.pars().parseSubsections(dim, imagePtr->ndim()) == duchamp::FAILURE){
	  // if here, something went wrong - try to detect and throw appropriately
	  if (this->itsCube.pars().section().parse(dim, imagePtr->ndim()) == duchamp::FAILURE)
	    ASKAPTHROW(AskapError, "Cannot parse the subsection string " << this->itsCube.pars().section().getSection());
	  if (this->itsCube.pars().getFlagStatSec() && this->itsCube.pars().statsec().parse(dim, imagePtr->ndim()) == duchamp::FAILURE)
	    ASKAPTHROW(AskapError, "Cannot parse the statistics subsection string " << this->itsCube.pars().statsec().getSection());
	  if(!this->itsCube.pars().section().isValid())
	    ASKAPTHROW(AskapError, "Pixel subsection " << this->itsBaseSubsection << " has no pixels");
	  if(this->itsCube.pars().getFlagStatSec() && !this->itsCube.pars().statsec().isValid())
	    ASKAPTHROW(AskapError, "Statistics subsection " << this->itsBaseStatSubsection << " has no pixels in common with the image or the pixel subsection requested");
	}
	
	if(this->itsComms.isMaster() & this->itsCube.pars().getFlagStatSec() && !this->itsCube.pars().statsec().isValid())
	  ASKAPTHROW(AskapError, "Statistics subsection has no valid pixels");
	
	ASKAPLOG_INFO_STR(logger, this->workerPrefix() << "Using subsection " << this->itsCube.pars().section().getSection());
	if(this->itsCube.pars().getFlagStatSec() && this->itsCube.pars().statsec().isValid())
	  ASKAPLOG_INFO_STR(logger, this->workerPrefix() << "Using stat-subsection " << this->itsCube.pars().statsec().getSection());
	

	Slicer slice = subsectionToSlicer(this->itsCube.pars().section());
	fixSlicer(slice, wcs);

	const SubImage<Float> *sub = new SubImage<Float>(*imagePtr, slice);
	
	return sub;
      }

      //**************************************************************//

      duchamp::OUTCOME DuchampParallel::getCasaMetadata(const ImageInterface<Float>*  imagePtr, DATATYPE typeOfData)
      {

	/// @details Read some basic metadata from the image, storing
	/// the WCS information, beam information, flux units, setting
	/// the is2D flag and fixing spectral units if need be. If we
	/// want METADATA, then the cube is initialised without
	/// allocation (ie. the dimension array is set and some
	/// parameter flags are checked). Otherwise, initialisation is
	/// saved till later.
	/// @param imagePtr The image, already opened
	/// @param typeOfData Either IMAGE or METADATA

	size_t *dim = getDim(imagePtr);
	wcsprm *wcs = casaImageToWCS(imagePtr);
	ASKAPLOG_DEBUG_STR(logger, this->workerPrefix() << "Defining WCS and putting into type \""<<this->itsCube.pars().getSpectralType()<<"\"");
	this->itsCube.header().defineWCS(wcs,1,dim,this->itsCube.pars());
	this->itsCube.pars().setOffsets(wcs);
	readBeamInfo(imagePtr, this->itsCube.header(), this->itsCube.pars());
	this->itsCube.header().setFluxUnits(imagePtr->units().getName());
	
	// check the true dimensionality and set the 2D flag in the cube header.
	this->itsCube.header().set2D(imagePtr->shape().nonDegenerate().size() <= 2);
	
	// set up the various flux units
	if (wcs->spec >= 0) this->itsCube.header().fixSpectralUnits(this->itsCube.pars().getSpectralUnits());

	this->itsCube.header().setIntFluxUnits();

	if(typeOfData == METADATA) this->itsCube.initialiseCube(dim, false);
	delete [] dim;
	return duchamp::SUCCESS;


      }


      //**************************************************************//




    }
}
