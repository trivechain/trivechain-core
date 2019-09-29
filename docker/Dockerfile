FROM debian:stretch
LABEL maintainer="Trivechain Developers <dev@trivechain.com>"
LABEL description="Dockerised Trivechain, built from Travis"

RUN apt-get update && apt-get -y upgrade && apt-get clean && rm -fr /var/cache/apt/*

COPY bin/* /usr/bin/
