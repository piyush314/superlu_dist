#!/bin/bash
# Bash script to submit many files to Cori/Edison/Queue

#BSUB -P CSC289
#BSUB -W 2:00
#BSUB -nnodes 45
#BSUB -alloc_flags "gpumps smt1"
#BSUB -J superlu_gpu




EXIT_SUCCESS=0
EXIT_HOST=1
EXIT_PARAM=2

module load essl
module load cmake/3.11.3
module load cuda/9.2.148

CUR_DIR=`pwd`
FILE_DIR=$CUR_DIR/EXAMPLE
INPUT_DIR=$MEMBERWORK/csc289/matrix
# INPUT_DIR=$CUR_DIR/../EXAMPLE						 
FILE_NAME=pddrive
FILE=$FILE_DIR/$FILE_NAME


#nprows=(  2  )
#npcols=(  3  )  
#RANK_PER_RS=1

nprows=(  6  )
npcols=(  7  )  
RANK_PER_RS=7




for ((i = 0; i < ${#npcols[@]}; i++)); do
NROW=${nprows[i]}
NCOL=${npcols[i]}

# NROW=36
CORE_VAL=`expr $NCOL \* $NROW`


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

for NTH in 1  
do

RS_VAL=`expr $CORE_VAL / $RANK_PER_RS`
MOD_VAL=`expr $CORE_VAL % $RANK_PER_RS`
if [[ $MOD_VAL -ne 0 ]]
then
  RS_VAL=`expr $RS_VAL + 1`
fi
OMP_NUM_THREADS=$NTH
TH_PER_RS=`expr $NTH \* $RANK_PER_RS`
GPU_PER_RS=1

# for MAT in copter2.mtx
 # for MAT in rajat16.mtx
# for MAT in ExaSGD/118_1536/globalmat.datnh
# for MAT in copter2.mtx gas_sensor.mtx matrix-new_3.mtx xenon2.mtx shipsec1.mtx xenon1.mtx g7jac160.mtx g7jac140sc.mtx mark3jac100sc.mtx ct20stif.mtx vanbody.mtx ncvxbqp1.mtx dawson5.mtx 2D_54019_highK.mtx gridgena.mtx epb3.mtx torso2.mtx torsion1.mtx boyd1.bin hvdc2.mtx rajat16.mtx hcircuit.mtx 
# for MAT in copter2.mtx gas_sensor.mtx matrix-new_3.mtx av41092.mtx xenon2.mtx c-71.mtx shipsec1.mtx xenon1.mtx g7jac160.mtx g7jac140sc.mtx mark3jac100sc.mtx ct20stif.mtx vanbody.mtx ncvxbqp1.mtx dawson5.mtx c-59.mtx 2D_54019_highK.mtx gridgena.mtx epb3.mtx torso2.mtx finan512.mtx twotone.mtx torsion1.mtx jan99jac120.mtx boyd1.mtx c-73b.mtx hvdc2.mtx rajat16.mtx hcircuit.mtx 
  # for MAT in matrix05_ntime=2/s1_mat_0_126936.bin
# for MAT in matrix05_ntime=2/s1_mat_0_126936.bin A30_P0/A30_015_0_25356.bin
# for MAT in matrix05_ntime=2/s1_mat_0_126936.bin
#for MAT in s1_mat_0_126936.bin

# for MAT in A30_P0/A30_015_0_25356.bin
 # for MAT in A64/A64_001_0_1204992.bin
  # for MAT in big.rua
# for MAT in  StocF-1465.bin 
# for MAT in /mathias/DG_GrapheneDisorder_8192.bin /mathias/DNA_715_64cell.bin /mathias/LU_C_BN_C_4by2.bin /mathias/Li4244.bin 
 for MAT in /mathias/DNA_715_64cell.bin /mathias/Li4244.bin 

# for MAT in LU_C_BN_C_4by2.bin Li4244.bin atmosmodj.bin Ga19As19H42.bin Geo_1438.bin StocF-1465.bin
  do
    export OMP_NUM_THREADS=$OMP_NUM_THREADS
    mkdir -p ${MAT}_summit
	echo "jsrun --nrs $RS_VAL --tasks_per_rs $RANK_PER_RS --cpu_per_rs $RANK_PER_RS -c $TH_PER_RS --gpu_per_rs $GPU_PER_RS  --rs_per_host 6 $FILE -c $NCOL -r $NROW $INPUT_DIR/$MAT | tee ./${MAT}_summit_new_LU/SLU.o_mpi_${NROW}x${NCOL}_OMP_${OMP_NUM_THREADS}_GPU_${GPU_PER_RANK}"
     jsrun --nrs $RS_VAL --tasks_per_rs $RANK_PER_RS --cpu_per_rs $RANK_PER_RS -c $TH_PER_RS --gpu_per_rs $GPU_PER_RS  --rs_per_host 6 '--smpiargs=-x PAMI_DISABLE_CUDA_HOOK=1 -disable_gpu_hooks' $FILE -c $NCOL -r $NROW $INPUT_DIR/$MAT | tee ./${MAT}_summit/SLU.o_mpi_${NROW}x${NCOL}_OMP_${OMP_NUM_THREADS}_GPU_${GPU_PER_RANK}
  done
#one

done
done
exit $EXIT_SUCCESS

