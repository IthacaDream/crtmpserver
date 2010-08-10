/* 
 *  Copyright (c) 2010,
 *  Gavriloaie Eugen-Andrei (shiretu@gmail.com)
 *
 *  This file is part of crtmpserver.
 *  crtmpserver is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  crtmpserver is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with crtmpserver.  If not, see <http://www.gnu.org/licenses/>.
 */



#ifdef HAS_PROTOCOL_VAR
#include "variantappprotocolhandler.h"
#include "protocols/variant/basevariantprotocol.h"

VariantAppProtocolHandler::VariantAppProtocolHandler(Variant &configuration)
: BaseVariantAppProtocolHandler(configuration) {

}

VariantAppProtocolHandler::~VariantAppProtocolHandler() {
}

bool VariantAppProtocolHandler::ProcessMessage(BaseVariantProtocol *pProtocol,
		Variant &lastSent, Variant &lastReceived) {
	if (pProtocol->GetFarProtocol()->GetType() == PT_INBOUND_HTTP) {
		return pProtocol->Send(lastReceived);
	} else {
		FINEST("lastSent:\n%s\nlastReceived:\n%s", STR(lastSent.ToString()), STR(lastReceived.ToString()));
		return true;
	}
}
#endif	/* HAS_PROTOCOL_VAR */

