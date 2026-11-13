#include "vma.hpp"

#include "flag.h"
#include "rope.h"
#include "vant.h"
#include "sail.h"
#include "script_func.h"

CREATE_CLASS(SAIL)

CREATE_CLASS(FLAG)

class PFl0 : public FLAG { public: PFl0() : FLAG("TexPiFName0", "TexPiFCount0") {} };
class PFC0 : public FLAG { public: PFC0() : FLAG("TexPiCName0", "TexPiCCount0") {} };
class CFl0 : public FLAG { public: CFl0() : FLAG("TexShpName0", "TexShpCount0") {} };
class FFl0 : public FLAG { public: FFl0() : FLAG("TexFrtName0", "TexFrtCount0") {} };
class MFl0 : public FLAG { public: MFl0() : FLAG("TexMerName0", "TexMerCount0") {} };
class PRl0 : public FLAG { public: PRl0() : FLAG("TexPerName0", "TexPerCount0") {} };
class QFl0 : public FLAG { public: QFl0() : FLAG("TexQstName0", "TexQstCount0") {} };

CREATE_CLASS(PFl0)
CREATE_CLASS(PFC0)
CREATE_CLASS(CFl0)
CREATE_CLASS(FFl0)
CREATE_CLASS(MFl0)
CREATE_CLASS(PRl0)
CREATE_CLASS(QFl0)

CREATE_CLASS(ROPE)

CREATE_CLASS(VANT)

CREATE_CLASS(VANTL)

CREATE_CLASS(VANTZ)

CREATE_SCRIPTLIBRIARY(SCRIPT_RIGGING_FILES)
