# Polonio Conformance Matrix

This is the definitive quick reference for RFC 0003 profiles. **Required** is
guaranteed by a profile; **Optional** may be supplied but is not guaranteed;
**Unavailable** is outside that profile. The Web Runtime requires its Layer 4
API surface, but CGI and `polonio serve` are optional adapters. `send_file`
and `send_mail` are required Web Runtime APIs and use the reference Data
Runtime storage model when run by the official distribution.

| Capability | Language Core | Standard Library | Template Runtime | Web Runtime | Data Runtime | Reference Distribution |
|---|---|---|---|---|---|---|
| Parser | Required | Required | Required | Required | Required | Required |
| Interpreter / execution | Required | Required | Required | Required | Required | Required |
| Functions and closures | Required | Required | Required | Required | Required | Required |
| Strings | Unavailable | Required | Optional | Required | Required | Required |
| Arrays / objects | Unavailable | Required | Optional | Required | Required | Required |
| Math | Unavailable | Required | Optional | Required | Required | Required |
| Dates | Unavailable | Required | Optional | Required | Required | Required |
| Template output | Unavailable | Unavailable | Required | Optional | Unavailable | Required |
| Templates | Unavailable | Unavailable | Required | Optional | Unavailable | Required |
| Interpolation | Unavailable | Unavailable | Required | Optional | Unavailable | Required |
| Includes | Unavailable | Unavailable | Required | Optional | Unavailable | Required |
| HTTP request | Unavailable | Unavailable | Unavailable | Required | Unavailable | Required |
| HTTP response | Unavailable | Unavailable | Unavailable | Required | Unavailable | Required |
| CGI adapter | Unavailable | Unavailable | Unavailable | Optional | Unavailable | Required |
| Development server | Unavailable | Unavailable | Unavailable | Optional | Unavailable | Required |
| Sessions | Unavailable | Unavailable | Unavailable | Required | Unavailable | Required |
| CSRF | Unavailable | Unavailable | Unavailable | Required | Unavailable | Required |
| Uploads | Unavailable | Unavailable | Unavailable | Required | Unavailable | Required |
| Storage | Unavailable | Unavailable | Unavailable | Optional | Required | Required |
| SQLite | Unavailable | Unavailable | Unavailable | Optional | Required | Required |
| `send_file` | Unavailable | Unavailable | Unavailable | Required | Unavailable | Required |
| `send_mail` | Unavailable | Unavailable | Unavailable | Required | Unavailable | Required |
