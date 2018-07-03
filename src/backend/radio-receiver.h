/*
 *    Copyright (C) 2018
 *    Matthias P. Braendli (matthias.braendli@mpb.li)
 *
 *    Copyright (C) 2017
 *    Albrecht Lohofener (albrechtloh@gmx.de)
 *
 *    This file is based on SDR-J
 *    Copyright (C) 2010, 2011, 2012
 *    Jan van Katwijk (J.vanKatwijk@gmail.com)
 *
 *    This file is part of the welle.io.
 *    Many of the ideas as implemented in welle.io are derived from
 *    other work, made available through the GNU general Public License.
 *    All copyrights of the original authors are recognized.
 *
 *    welle.io is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    welle.io is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with welle.io; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef RADIO_RECEIVER_H
#define RADIO_RECEIVER_H

#include <memory>
#include <string>
#include "radio-controller.h"
#include "fic-handler.h"
#include "msc-handler.h"
#include "ofdm/ofdm-processor.h"

class RadioReceiver {
    public:
        RadioReceiver(
                RadioControllerInterface& rci,
                InputInterface& input,
                int ofdmProcessorThreshold = 3,
                bool decodeTII = false);

        /* Restart the receiver, and specify if we want
         * to scan or receive. */
        void restart(bool doScan);

        /* Play the audio component of the service. Returns true if an
         * audio subchannel was found and tuned to. */
        bool playSingleProgramme(ProgrammeHandlerInterface& handler,
                const std::string& dumpFileName, const Service& s);

        bool addServiceToDecode(ProgrammeHandlerInterface& handler,
                const std::string& dumpFileName, const Service& s);

        bool removeServiceToDecode(const Service& s);

        uint16_t getEnsembleId(void) const;
        DabLabel getEnsembleLabel(void) const;
        std::vector<Service> getServiceList(void) const;
        std::list<ServiceComponent> getComponents(const Service& s) const;

        /* Return the subchannel corresponding to the given component.
         * This can fail, in which case the Subchannel returned has
         * subch field equals to -1
         */
        Subchannel getSubchannel(const ServiceComponent& sc) const;

    private:
        bool playProgramme(ProgrammeHandlerInterface& handler,
                const Service& s,
                const std::string& dumpFileName,
                bool unique);

        DABParams params; // Defaults to TM1 parameters

        MscHandler mscHandler;
        FicHandler ficHandler;
        OFDMProcessor ofdmProcessor;
};

#endif
