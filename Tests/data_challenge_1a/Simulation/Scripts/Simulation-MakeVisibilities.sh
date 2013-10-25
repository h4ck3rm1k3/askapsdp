#!/bin/bash -l

mkdir -p ${msdir}
mkdir -p ${parsetdirVis}
mkdir -p ${logdirVis}

cd ${visdir}
WORKDIR=run${RUN_NUM}
mkdir -p ${WORKDIR}
cd ${WORKDIR}
touch ${now}

##############################
# Parameters & Definitions
##############################

dependVis=""
runOK=true

if [ $doCsim == true ]; then

    if [ $doVisCleanup == true ]; then
	
	if [ -e ${failureListVis} ]; then
	    INDEX="\`head -\${PBS_ARRAY_INDEX} ${failureListVis} | tail -1\`"
	    qsubCmd="qsub -J 1-`wc -l ${failureListVis} | awk '{print $1}'` "
	else
	    echo "Visibility failure list ${failureListVis} does not exist. Not running"
	    runOK=false
	fi
	
    else

	INDEX="\${PBS_ARRAY_INDEX}"
	qsubCmd="qsub -J 0-`expr $numMSchunks - 1` "
	
    fi 


##############################
# Qsub & Parset definition
##############################

    qsubfile=${visdir}/${WORKDIR}/makeVis.qsub

    if [ $doCorrupt == true ]; then
	# Create the random gains parset
	${ASKAP_ROOT}/Code/Components/Synthesis/synthesis/current/apps/randomgains.sh ${randomgainsArgs} ${calibparset}
    fi

    cat > $qsubfile <<EOF
#!/bin/bash -l
#PBS -W group_list=astronomy554
#PBS -l walltime=6:00:00
${csimSelect}
#PBS -M matthew.whiting@csiro.au
#PBS -N mkVis
#PBS -m bea
#PBS -j oe

cd \$PBS_O_WORKDIR
export ASKAP_ROOT=${ASKAP_ROOT}
export AIPSPATH=\${ASKAP_ROOT}/Code/Base/accessors/current
csim=\${ASKAP_ROOT}/Code/Components/Synthesis/synthesis/current/apps/csimulator.sh
rndgains=\${ASKAP_ROOT}/Code/Components/Synthesis/synthesis/current/apps/randomgains.sh
askapconfig=\${ASKAP_ROOT}/Code/Components/Synthesis/testdata/current/simulation/stdtest/definitions

IND=${INDEX}

dir="csim-\`echo \${PBS_JOBID} | sed -e 's/\[[0-9]*\]//g'\`"
mkdir -p ${parsetdirVis}/\${dir}
mkdir -p ${logdirVis}/\${dir}

ms=${msChunk}_\${IND}.ms
skymodel=${slicebase}\${IND}
modelInChunks=${writeByNode}
if [ \$modelInChunks == "true" ]; then
# Model was created with writeByNode=true, so we need to do the
# extraction of the appropriate channel from each chunk and paste
# together

    pyscript=${parsetdirVis}/\${dir}/modelExtract_chan\${IND}.py
    cat > \$pyscript <<EOF_INNER
import fnmatch
import numpy as np
goodfiles=[]
baseimage='${baseimage}'
for file in os.listdir('${chunkdir}'):
    if fnmatch.fnmatch(file,'%s_w*__'%baseimage):
        goodfiles.append(file)
goodfiles.sort()

ia.open('${chunkdir}/%s'%goodfiles[0])
crec=ia.coordsys().torecord()
crec['direction0']['crpix']=np.array([${npix}/2.,${npix}/2.])
ia.close()
ia.newimagefromshape(outfile='\${skymodel}',shape=[${npix},${npix},1,1],csys=crec)

for file in goodfiles:
    offset=np.array(file.split('__')[1].split('_'),dtype=int)
    ia.open('${chunkdir}/%s'%file)
    shape=ia.shape()
    blc=np.zeros(len(shape),dtype=int).tolist()
    trc=(np.array(shape,dtype=int)-1).tolist()
    blc[3]=\${IND}
    trc[3]=\${IND}
    print file,offset,blc,trc
    chunk=ia.getchunk(blc=blc,trc=trc)
    ia.close()
    ia.open('\${skymodel}')
    ia.putchunk(pixels=chunk,blc=offset.tolist())
    ia.close()

EOF_INNER

    pylog=${logdirVis}/\${dir}/modelExtract_chan\${IND}.log
    casapy --nologger --log2term -c \${pyscript} > \$pylog

fi

nurefMHz=\`echo ${rfreq} \${IND} ${chanPerMSchunk} ${rchan} ${chanw} | awk '{printf "%13.8f",(\$1+(\$2*\$3-\$4)*\$5)/1.e6}'\`
spw="[${chanPerMSchunk}, \${nurefMHz} MHz, ${chanw} Hz, \"${pol}\"]"

VarNoise=${varNoise}
Tsys=${tsys}
if [ \${VarNoise} == true ]; then
    Tsys=\`echo \$nurefMHz $noiseSlope $noiseIntercept $freqTsys50 | awk '{if (\$1>\$4) printf "%4.1f",(\$1 * \$2) + \$3; else printf "50.0"}'\`
fi

mkVisParset=${parsetdirVis}/\${dir}/csim-\${PBS_JOBID}.in
mkVisLog=${logdirVis}/\${dir}/csim-\${PBS_JOBID}.log

cat > \${mkVisParset} << EOF_INNER
Csimulator.dataset                              =       \$ms
#
Csimulator.stman.bucketsize                     =       2097152
#
Csimulator.sources.names                        =       [DCmodel]
Csimulator.sources.DCmodel.direction            =       [12h30m00.000, ${decStringVis}, J2000]
Csimulator.sources.DCmodel.model                =       \${skymodel}
#
# Define the antenna locations, feed locations, and spectral window definitions
#
Csimulator.antennas.definition                   =       \${askapconfig}/${array}
Csimulator.feeds.definition                      =       \${askapconfig}/${feeds}
Csimulator.spws.names                            =       [thisSPWS]
Csimulator.spws.thisSPWS                         =       \${spw}
#						 
Csimulator.simulation.blockage                   =       0.01
Csimulator.simulation.elevationlimit             =       8deg
Csimulator.simulation.autocorrwt                 =       0.0
Csimulator.simulation.usehourangles              =       True
Csimulator.simulation.referencetime              =       [2010Jan30, UTC]
#						 
Csimulator.simulation.integrationtime            =       ${inttime}
#						 
Csimulator.observe.number                        =       1
Csimulator.observe.scan0                         =       [DCmodel, thisSPWS, -${dur}h, ${dur}h]
#
Csimulator.gridder                               =       ${gridder}
Csimulator.gridder.padding                       =       ${pad}
Csimulator.gridder.snapshotimaging               =       ${doSnapshot}
Csimulator.gridder.snapshotimaging.wtolerance    =       ${wtol}
Csimulator.gridder.${gridder}.wmax               =       ${wmax}
Csimulator.gridder.${gridder}.nwplanes           =       ${nw}
Csimulator.gridder.${gridder}.oversample         =       ${os}
Csimulator.gridder.${gridder}.diameter           =       12m
Csimulator.gridder.${gridder}.blockage           =       2m
Csimulator.gridder.${gridder}.maxsupport         =       ${maxsup}
Csimulator.gridder.${gridder}.maxfeeds           =       ${nfeeds}
Csimulator.gridder.${gridder}.frequencydependent =       false
Csimulator.gridder.${gridder}.variablesupport    =       true 
Csimulator.gridder.${gridder}.offsetsupport      =       true 
#
Csimulator.noise                                 =       ${doNoise}
Csimulator.noise.Tsys                            =       \${Tsys}
Csimulator.noise.efficiency                      =       0.8   
Csimulator.noise.seed1                           =       time
Csimulator.noise.seed2                           =       \${IND}
#
Csimulator.corrupt                               =       ${doCorrupt}
Csimulator.calibaccess                           =       parset
Csimulator.calibaccess.parset                    =       ${calibparset}
EOF_INNER

mpirun \${csim} -c \${mkVisParset} > \${mkVisLog}
err=\$?
exit \$err

EOF

    if [ $doSubmit == true ] && [ $runOK == true ]; then
	
	visID=`$qsubCmd ${depend} $qsubfile`
	dependVis="-W depend=afterok:${visID}"
	
    fi

fi


##########################
# Merging of visibilities

if [ $doMergeVis == true ]; then

    if [ $doClobberMergedVis == true ]; then

	rm -rf ${msStage1base}_*
	rm -rf ${finalMS}

    fi

    if [ $doMergeStage1 == true ]; then

	merge1qsub=${visdir}/${WORKDIR}/mergeVisStage1.qsub
    
	cat > $merge1qsub <<EOF
#!/bin/bash
#PBS -W group_list=astronomy554
#PBS -l select=1:ncpus=1:mem=2GB:mpiprocs=1
#PBS -l walltime=12:00:00
#PBS -M matthew.whiting@csiro.au
#PBS -N visMerge1
#PBS -m a
#PBS -j oe

#######
# TO RUN (${numStage1jobs} jobs):
#  qsub -J 1-${numStage1jobs} stage1.qsub
#######

cd \$PBS_O_WORKDIR

MSPERJOB=${msPerStage1job}

START=\`echo \${PBS_ARRAY_INDEX} \$MSPERJOB | awk '{print (\$1-1)*\$2}'\`
END=\`expr \${START} + \${MSPERJOB}\`

IDX=\$START
unset FILES
while [ \$IDX -lt \$END ]; do
    FILES="\$FILES ${msChunk}_\${IDX}.ms" 
    IDX=\`expr \$IDX + 1\`
done

dir="merge1-\`echo \${PBS_JOBID} | sed -e 's/\[[0-9]*\]//g'\`"
mkdir -p ${logdirVis}/\${dir}
logfile=${logdirVis}/\${dir}/merge_s1_output_\${PBS_JOBID}.log
echo "Start = \$START, End = \$END" > \${logfile}
echo "Processing files: \$FILES" >> \${logfile}
$ASKAP_ROOT/Code/Components/Synthesis/synthesis/current/apps/msmerge.sh -o ${msStage1}_\${PBS_ARRAY_INDEX}.ms \$FILES >> \${logfile}

EOF

	if [ $doSubmit == true ] && [ $runOK == true ]; then
	    
	    merge1ID=`qsub ${dependVis} -J 1-${numStage1jobs} $merge1qsub`
	    
	    if [ "$dependVis" == "" ]; then
		dependVis="-W depend=afterok:${merge1ID}"
	    else
		dependVis="${dependVis}:${merge1ID}"
	    fi
	    
	fi

    fi

####

    if [ $doMergeStage2 == true ]; then

	merge2qsub=${visdir}/${WORKDIR}/mergeVisStage2.qsub

	cat > $merge2qsub <<EOF
#!/bin/bash
#PBS -W group_list=astronomy554
#PBS -l select=1:ncpus=1:mem=8GB:mpiprocs=1
#PBS -l walltime=12:00:00
#PBS -M matthew.whiting@csiro.au
#PBS -N visMerge2
#PBS -m a
#PBS -j oe

cd \$PBS_O_WORKDIR

IDX=1
unset FILES
while [ \$IDX -le ${numStage1jobs} ]; do
    FILES="\$FILES ${msStage1}_\${IDX}.ms" 
    IDX=\`expr \$IDX + 1\`
done

logfile=${logdirVis}/merge_s2_output_\${PBS_JOBID}.log
echo "Processing files: \$FILES" > \${logfile}
$ASKAP_ROOT/Code/Components/Synthesis/synthesis/current/apps/msmerge.sh -o ${finalMS} \$FILES >> \${logfile}
EOF

	if [ $doSubmit == true ] && [ $runOK == true ]; then

	    merge2ID=`qsub ${dependVis} $merge2qsub`

	fi

    fi

fi

cd ${BASEDIR}
