#pragma once

#define GET_TYPE_STRING(TypeName) \
	((void)sizeof(UEAsserts_Private::GetMemberNameCheckedJunk((TypeName*)0)), TEXT(#TypeName))

