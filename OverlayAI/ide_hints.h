// Small header to help Visual Studio browsing and IntelliSense with problematic SDK macros.
// This file defines macros that can confuse code browsing. It is intentionally minimal
// and only affects IDE browsing behavior. Keep it lightweight and safe for builds.

#ifndef OVERLAYAI_IDE_HINTS_H
#define OVERLAYAI_IDE_HINTS_H

// VCR102: Browsing operations around this macro may fail. Define it for the IDE to
// improve browsing without changing build semantics in most cases.
#ifndef DECLSPEC_NOINITALL
#define DECLSPEC_NOINITALL
#endif

#endif // OVERLAYAI_IDE_HINTS_H
