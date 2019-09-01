#!/bin/bash
# Bash script to submit many files to Cori/Edison/Queue

#BSUB -P CSC289
#BSUB -W 2:00
#BSUB -nnodes 45
#BSUB -alloc_flags gpumps
#BSUB -J superlu_gpu




EXIT_SUCCESS=0
EXIT_HOST=1
EXIT_PARAM=2

module load essl
module load cmake/3.11.3
module load cuda/9.2.148

module use /gpfs/alpine/csc322/world-shared/modulefiles
module load hpctoolkit

CUR_DIR=`pwd`
FILE_DIR=$CUR_DIR/EXAMPLE
INPUT_DIR=$MEMBERWORK/csc289/matrix
FILE_NAME=pddrive
FILE=$FILE_DIR/$FILE_NAME


nprows=(1  2 4 )
npcols=(1  2 4 )  
 
for ((i = 0; i < ${#npcols[@]}; i++)); do
NROW=${nprows[i]}
NCOL=${npcols[i]}

# NROW=36
CORE_VAL=`expr $NCOL \* $NROW`
RANK_PER_RS=1
GPU_PER_RANK=0


#PARTITION=debug
PARTITION=regular
LICENSE=SCRATCH
TIME=00:20:00

if [[ $NERSC_HOST == edison ]]
then
  CONSTRAINT=0
fi
if [[ $NERSC_HOST == cori ]]
then
  CONSTRAINT=haswell
fi

for GPU_PER_RANK in  0
do
for NTH in 1 4 16 
do

RS_VAL=`expr $CORE_VAL / $RANK_PER_RS`
MOD_VAL=`expr $CORE_VAL % $RANK_PER_RS`
if [[ $MOD_VAL -ne 0 ]]
then
  RS_VAL=`expr $RS_VAL + 1`
fi
OMP_NUM_THREADS=$NTH
TH_PER_RS=`expr $NTH \* $RANK_PER_RS`
GPU_PER_RS=`expr $RANK_PER_RS \* $GPU_PER_RANK`

# for MAT in copter2.mtx
 # for MAT in rajat16.mtx
# for MAT in ExaSGD/118_1536/globalmat.datnh
# for MAT in copter2.mtx gas_sensor.mtx matrix-new_3.mtx xenon2.mtx shipsec1.mtx xenon1.mtx g7jac160.mtx g7jac140sc.mtx mark3jac100sc.mtx ct20stif.mtx vanbody.mtx ncvxbqp1.mtx dawson5.mtx 2D_54019_highK.mtx gridgena.mtx epb3.mtx torso2.mtx torsion1.mtx boyd1.bin hvdc2.mtx rajat16.mtx hcircuit.mtx 
# for MAT in copter2.mtx gas_sensor.mtx matrix-new_3.mtx av41092.mtx xenon2.mtx c-71.mtx shipsec1.mtx xenon1.mtx g7jac160.mtx g7jac140sc.mtx mark3jac100sc.mtx ct20stif.mtx vanbody.mtx ncvxbqp1.mtx dawson5.mtx c-59.mtx 2D_54019_highK.mtx gridgena.mtx epb3.mtx torso2.mtx finan512.mtx twotone.mtx torsion1.mtx jan99jac120.mtx boyd1.mtx c-73b.mtx hvdc2.mtx rajat16.mtx hcircuit.mtx 
  # for MAT in matrix05_ntime=2/s1_mat_0_126936.bin
# for MAT in matrix05_ntime=2/s1_mat_0_126936.bin A30_P0/A30_015_0_25356.bin
# for MAT in matrix05_ntime=2/s1_mat_0_126936.bin
for MAT in s1_mat_0_126936.bin

# for MAT in A30_P0/A30_015_0_25356.bin
 # for MAT in A64/A64_001_0_1204992.bin
#   for MAT in big.rua
# for MAT in  StocF-1465.bin 
# for MAT in /mathias/DG_GrapheneDisorder_8192.bin /mathias/DNA_715_64cell.bin /mathias/LU_C_BN_C_4by2.bin /mathias/Li4244.bin 

# for MAT in LU_C_BN_C_4by2.bin Li4244.bin atmosmodj.bin Ga19As19H42.bin Geo_1438.bin StocF-1465.bin
  do
    export OMP_NUM_THREADS=$OMP_NUM_THREADS
    mkdir -p ${MAT}_summit
	OUTPUT=./${MAT}_summit/SLU.o_mpi_${NROW}x${NCOL}_OMP_${OMP_NUM_THREADS}_GPU_${GPU_PER_RANK}
	rm -rf $OUTPUT
	mkdir -p $MEMBERWORK/csc289/tmp
	cp -r $FILE $MEMBERWORK/csc289/tmp/.
	cd $MEMBERWORK/csc289/tmp
    rm -rf ${MAT}_mpi_${NROW}x${NCOL}_OMP_${OMP_NUM_THREADS}_GPU_${GPU_PER_RANK}
    rm -rf ${MAT}_mpi_${NROW}x${NCOL}_OMP_${OMP_NUM_THREADS}_GPU_${GPU_PER_RANK}_dat
	
	echo "jsrun -n $RS_VAL -a $RANK_PER_RS -c $TH_PER_RS -g $GPU_PER_RS -b packed:$NTH hpcrun -t -e REALTIME -o ${MAT}_mpi_${NROW}x${NCOL}_OMP_${OMP_NUM_THREADS}_GPU_${GPU_PER_RANK} ./pddrive -c $NCOL -r $NROW $INPUT_DIR/$MAT | tee -a $FILE_DIR/../$OUTPUT"
	jsrun -n $RS_VAL -a $RANK_PER_RS -c $TH_PER_RS -g $GPU_PER_RS -b packed:$NTH hpcrun -t -e REALTIME -o ${MAT}_mpi_${NROW}x${NCOL}_OMP_${OMP_NUM_THREADS}_GPU_${GPU_PER_RANK} ./pddrive -c $NCOL -r $NROW $INPUT_DIR/$MAT | tee -a $FILE_DIR/../$OUTPUT
#	jsrun -n $RS_VAL -a $RANK_PER_RS -c $TH_PER_RS -g $GPU_PER_RS -l CPU-CPU -d packed -b rs hpcrun -t -e REALTIME -o ${MAT}_mpi_${NROW}x${NCOL}_OMP_${OMP_NUM_THREADS}_GPU_${GPU_PER_RANK} ./pddrive -c $NCOL -r $NROW $INPUT_DIR/$MAT | tee -a $FILE_DIR/../$OUTPUT
  
	hpcstruct ./pddrive
	hpcprof-mpi -S ./pddrive.hpcstruct -o ${MAT}_mpi_${NROW}x${NCOL}_OMP_${OMP_NUM_THREADS}_GPU_${GPU_PER_RANK}_dat -I/$FILE_DIR/../../SRC/'*' ${MAT}_mpi_${NROW}x${NCOL}_OMP_${OMP_NUM_THREADS}_GPU_${GPU_PER_RANK}   
    cp -r ${MAT}_mpi_${NROW}x${NCOL}_OMP_${OMP_NUM_THREADS}_GPU_${GPU_PER_RANK}_dat $FILE_DIR/../.
  done
#one

done
done
done
exit $EXIT_SUCCESS

