# Movie WebCam QR pairing

Click **Pair Phone** in Movie's main toolbar (also available in Track Inspector).
Movie discovers suitable active local endpoints and displays one secure QR code.
In Android WebCam 0.0.5 or newer, choose **Scan pairing QR code** and scan it.
Connection attempts begin automatically; grant camera/microphone access if asked.
No IP selection, address comparison, or copying is required. Manual invitation
entry remains an optional fallback on the phone.

Both devices need a reachable local network path. Routed private networks are
allowed; matching address prefixes are neither required nor sufficient. The
pairing window shows Waiting, Connecting, Connected or Failed, with guidance.
Keep it open until connected. Closing an unconnected window revokes pairing;
closing after connection leaves the stream active. Disconnect is explicit.

The invitation is single-use and expires after 120 seconds. New QR code revokes
the previous generation. No invitation is copied to clipboard, persisted in the
project or sent to a QR service. The existing MIT-licensed Nayuki generator
renders integer black/white modules with a four-module quiet zone.

See [implementation and verification](AUTOMATIC_PHONE_PAIRING.md) for adapter
selection, limitations and the physical Galaxy S23 Ultra acceptance procedure.
