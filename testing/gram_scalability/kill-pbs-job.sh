#!/bin/sh

OPTIND=0
while getopts "h:p:i:v" arg ; do
    if [ $arg = "f" ]; then
        host=$OPTARG
    elif [ $arg = "p" ]; then
        port=$OPTARG
    elif [ $arg = "i" ]; then
        job_id=$OPTARG
    elif [ $arg = "v" ]; then
        verbose=-v
    fi
done

if [ -z $host ]; then
    host=`${GLOBUS_LOCATION}/libexec/globus-libc-hostname`
fi
if [ -z $port ]; then
    port=`./uhe-port.sh`
fi
if [ -z $job_id ]; then
    echo "ERROR: No job ID specified"
    exit
fi

factory="http://$host:$port/ogsa/services/base/gram/PbsManagedJobFactoryService"

./kill-job.sh -u $factory/$job_id $verbose
