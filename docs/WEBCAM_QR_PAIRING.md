# Movie WebCam QR pairing

Movie displays the existing one-use private LAN invitation as a local QR code.
In Track Inspector choose Pair/disconnect, then the PC LAN interface. On the
phone choose Scan QR in Djehuti WebCam, scan Movie's code and tap Connect.
Both devices must be on the same non-isolated LAN. No phone update is required.

The pairing dialog shows a 120-second countdown. Expired or used QR codes are
cleared. New QR code creates a new invitation and invalidates the previous one;
it is disabled during negotiation or connection to avoid replacing a live peer.
Closing a pending pairing dialog revokes the unused invitation; closing after
connection leaves streaming active. No secret is copied to the clipboard,
written to a file/project, or sent to an external QR service.

The MIT-licensed Nayuki generator is copied app-locally from the existing Suite
Remote Receiver dependency, preserving its embedded license. Rendering uses
integer-sized modules, black on white, with a four-module quiet zone.

Physical phone scanning and the Windows firewall/network environment remain
hands-on validation items. This change does not alter the media/timing protocol.
