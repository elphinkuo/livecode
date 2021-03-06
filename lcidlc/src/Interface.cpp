/* Copyright (C) 2003-2013 Runtime Revolution Ltd.

This file is part of LiveCode.

LiveCode is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License v3 as published by the Free
Software Foundation.

LiveCode is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with LiveCode.  If not see <http://www.gnu.org/licenses/>.  */

#include <stdio.h>
#include <string.h>

#include "Interface.h"
#include "InterfacePrivate.h"

////////////////////////////////////////////////////////////////////////////////

enum InterfaceError
{
	kInterfaceErrorNone,
	kInterfaceErrorUnknownUse,
	kInterfaceErrorTargetAlreadyUsed,
	kInterfaceErrorUnknownHook,
	kInterfaceErrorHookAlreadyDefined,
	kInterfaceErrorInvalidEnumName,
	kInterfaceErrorEnumAlreadyDefined,
	kInterfaceErrorEnumElementAlreadyDefined,
	kInterfaceErrorCannotMixHandlerTypes,
	kInterfaceErrorAmbiguousHandler,
	kInterfaceErrorParamAfterOptionalParam,
	kInterfaceErrorParamAlreadyDefined,
	kInterfaceErrorInvalidParameterType,
	kInterfaceErrorOptionalParamImpliesIn,
	kInterfaceErrorNoOptionalBoolean,
	kInterfaceErrorUnknownType,
};

////////////////////////////////////////////////////////////////////////////////

static const char *s_interface_native_types[] =
{
	"boolean",
	"c-string",
	"c-data",
	"integer",
	"real",
	
	"objc-string",
	"objc-number",
	"objc-data",
	"objc-array",
	"objc-dictionary",
	
	/*"cf-string",
	"cf-number",
	"cf-data",
	"cf-array",
	"cf-dictionary",*/
	
	nil
};

////////////////////////////////////////////////////////////////////////////////

extern const char *g_input_filename;

static bool InterfaceReport(InterfaceRef self, Position p_where, InterfaceError p_error, void *p_hint)
{
	fprintf(stderr, "%s:%d:%d: error: ", g_input_filename, PositionGetRow(p_where), PositionGetColumn(p_where));
	
	switch(p_error)
	{
	case kInterfaceErrorUnknownUse:
		fprintf(stderr, "Unknown use '%s'\n", StringGetCStringPtr(NameGetString((NameRef)p_hint)));
		break;
	case kInterfaceErrorTargetAlreadyUsed:
		fprintf(stderr, "Function '%s' already bound\n", StringGetCStringPtr(NameGetString((NameRef)p_hint)));
		break;
	case kInterfaceErrorUnknownHook:
		fprintf(stderr, "Unknown hook '%s'\n", StringGetCStringPtr(NameGetString((NameRef)p_hint)));
		break;
	case kInterfaceErrorHookAlreadyDefined:
		fprintf(stderr, "Hook '%s' already defined\n", StringGetCStringPtr(NameGetString((NameRef)p_hint)));
		break;
	case kInterfaceErrorInvalidEnumName:
		fprintf(stderr, "Type name '%s' is reserved\n", StringGetCStringPtr(NameGetString((NameRef)p_hint)));
		break;
	case kInterfaceErrorEnumAlreadyDefined:
		fprintf(stderr, "Enum with name '%s' already defined\n", StringGetCStringPtr(NameGetString((NameRef)p_hint)));
		break;
	case kInterfaceErrorEnumElementAlreadyDefined:
		fprintf(stderr, "Enum already has an element with name '%s'\n", StringGetCStringPtr((StringRef)p_hint));
		break;
	case kInterfaceErrorCannotMixHandlerTypes:
		fprintf(stderr, "Variants of handler '%s' have different type\n", StringGetCStringPtr(NameGetString((NameRef)p_hint)));
		break;
	case kInterfaceErrorAmbiguousHandler:
		fprintf(stderr, "Variant of handler '%s' is ambiguous\n", StringGetCStringPtr(NameGetString((NameRef)p_hint)));
		break;
	case kInterfaceErrorParamAfterOptionalParam:
		fprintf(stderr, "Cannot define non-optional parameter after optional parameters\n");
		break;
	case kInterfaceErrorParamAlreadyDefined:
		fprintf(stderr, "Handler already has a parameter with name '%s'\n", StringGetCStringPtr(NameGetString((NameRef)p_hint)));
		break;
	case kInterfaceErrorInvalidParameterType:
		fprintf(stderr, "Invalid combination of parameter type and mode\n");
		break;
	case kInterfaceErrorOptionalParamImpliesIn:
		fprintf(stderr, "Optional parameters must be of 'in' type\n");
		break;
	case kInterfaceErrorNoOptionalBoolean:
		fprintf(stderr, "Optional boolean parameters not yet supported\n");
		break;
	case kInterfaceErrorUnknownType:
		fprintf(stderr, "Unknown type '%s'\n", StringGetCStringPtr(NameGetString((NameRef)p_hint)));
		break;
	}

	self -> invalid = true;
	
	return true;
}

static void InterfaceCheckType(InterfaceRef self, Position p_where, NameRef p_type)
{
	for(uint32_t i = 0; s_interface_native_types[i] != nil; i++)
		if (NameEqualToCString(p_type, s_interface_native_types[i]))
			return;
			
	for(uint32_t i = 0; i < self -> enum_count; i++)
		if (NameEqual(self -> enums[i] . name, p_type))
			return;
			
	InterfaceReport(self, p_where, kInterfaceErrorUnknownType, p_type);
}

////////////////////////////////////////////////////////////////////////////////

bool InterfaceCreate(InterfaceRef& r_interface)
{
	return MCMemoryNew(r_interface);
}

void InterfaceDestroy(InterfaceRef self)
{
	ValueRelease(self -> name);
	
	for(uint32_t i = 0; i < self -> enum_count; i++)
	{
		ValueRelease(self -> enums[i] . name);
		for(uint32_t j = 0; j < self -> enums[i] . element_count; j++)
			ValueRelease(self -> enums[i] . elements[j] . key);
		MCMemoryDeleteArray(self -> enums[i] . elements);
	}
	MCMemoryDeleteArray(self -> enums);
	
	for(uint32_t i = 0; i < self -> handler_count; i++)
	{
		ValueRelease(self -> handlers[i] . name);
		for(uint32_t j = 0; j < self -> handlers[i] . variant_count; j++)
		{
			for(uint32_t k = 0; k < self -> handlers[i] . variants[j] . parameter_count; k++)
			{
				ValueRelease(self -> handlers[i] . variants[j] . parameters[k] . name);
				ValueRelease(self -> handlers[i] . variants[j] . parameters[k] . type);
				ValueRelease(self -> handlers[i] . variants[j] . parameters[k] . default_value);
			}
			MCMemoryDeleteArray(self -> handlers[i] . variants[j] . parameters);
			
			
			ValueRelease(self -> handlers[i] . variants[j] . return_type);
			ValueRelease(self -> handlers[i] . variants[j] . binding);
		}
		MCMemoryDeleteArray(self -> handlers[i] . variants);
	}
	MCMemoryDeleteArray(self -> handlers);
}

bool InterfaceBegin(InterfaceRef self, Position p_where, NameRef p_name)
{
	MCLog("%s - external %s", PositionDescribe(p_where), StringGetCStringPtr(NameGetString(p_name)));
	
	const char *t_unqualified_name;
	t_unqualified_name = strrchr(NameGetCString(p_name), '.');
	if (t_unqualified_name != nil)
		t_unqualified_name += 1;
	else
		t_unqualified_name = NameGetCString(p_name);
	
	if (!NameCreateWithNativeChars(t_unqualified_name, strlen(t_unqualified_name), self -> name))
		return false;
	
	self -> where = p_where;
	self -> qualified_name = ValueRetain(p_name);
	
	return true;
}

bool InterfaceEnd(InterfaceRef self)
{
	return true;
}

bool InterfaceDefineUse(InterfaceRef self, Position p_where, NameRef p_use)
{
	MCLog("%s - use %s", PositionDescribe(p_where), StringGetCStringPtr(NameGetString(p_use)));
	
	if (NameEqualToCString(p_use, "c++-exceptions"))
		self -> use_cpp_exceptions = true;
	else if (NameEqualToCString(p_use, "c++-naming"))
		self -> use_cpp_naming = true;
	else if (NameEqualToCString(p_use, "objc-exceptions"))
		self -> use_objc_exceptions = true;
	else if (NameEqualToCString(p_use, "objc-objects"))
		self -> use_objc_objects = true;
	else
		return InterfaceReport(self, p_where, kInterfaceErrorUnknownUse, p_use);
	
	return true;
}

bool InterfaceDefineHook(InterfaceRef self, Position p_where, NameRef p_handler, NameRef p_target)
{
	MCLog("%s - hook %s with %s", PositionDescribe(p_where), StringGetCStringPtr(NameGetString(p_handler)), StringGetCStringPtr(NameGetString(p_target)));
	
	// RULE: Hook must be one of 'startup' or 'shutdown'
	ValueRef *t_hook_var;
	t_hook_var = nil;
	if (NameEqualToCString(p_handler, "startup"))
		t_hook_var = &self -> startup_hook;
	else if (NameEqualToCString(p_handler, "shutdown"))
		t_hook_var = &self -> shutdown_hook;
	else
		return InterfaceReport(self, p_where, kInterfaceErrorUnknownHook, p_handler);
	
	// RULE: Hook must not be previous defined.
	if (*t_hook_var != nil)
		return InterfaceReport(self, p_where, kInterfaceErrorHookAlreadyDefined, p_handler);
	
	// RULE: Hook must target unique method.
	if (NameEqual(self -> startup_hook, p_target) ||
		NameEqual(self -> shutdown_hook, p_target))
		return InterfaceReport(self, p_where, kInterfaceErrorTargetAlreadyUsed, p_handler);
	
	*t_hook_var = ValueRetain(p_target);
	
	return true;
}

bool InterfaceBeginEnum(InterfaceRef self, Position p_where, NameRef p_name)
{
	MCLog("%s - enum %s", PositionDescribe(p_where), StringGetCStringPtr(NameGetString(p_name)));
	
	// RULE: Enum must not used built-in type name
	for(uint32_t i = 0; s_interface_native_types[i] != nil; i++)
		if (NameEqualToCString(p_name, s_interface_native_types[i]))
			InterfaceReport(self, p_where, kInterfaceErrorInvalidEnumName, p_name);
			
	// RULE: Enum must not already exist
	for(uint32_t i = 0; i < self -> enum_count; i++)
		if (NameEqual(self -> enums[i] . name, p_name))
			InterfaceReport(self, p_where, kInterfaceErrorEnumAlreadyDefined, p_name);
			
	if (!MCMemoryResizeArray(self -> enum_count + 1, self -> enums, self -> enum_count))
		return false;
		
	self -> enums[self -> enum_count - 1] . where = p_where;
	self -> enums[self -> enum_count - 1] . name = ValueRetain(p_name);
	
	return true;
}

bool InterfaceEndEnum(InterfaceRef self)
{
	return true;
}

bool InterfaceDefineEnumElement(InterfaceRef self, Position p_where, StringRef p_element, ValueRef p_value)
{
	MCLog("%s - enum element %s as %lld", PositionDescribe(p_where), StringGetCStringPtr(p_element), NumberGetInteger(p_value));
	
	Enum *t_enum;
	t_enum = &self -> enums[self -> enum_count - 1];
	
	// RULE: Element name must not already be present
	for(uint32_t i = 0; i < t_enum -> element_count; i++)
		if (MCCStringEqualCaseless(StringGetCStringPtr(p_element), StringGetCStringPtr(t_enum -> elements[i] . key)))
			return InterfaceReport(self, p_where, kInterfaceErrorEnumElementAlreadyDefined, p_element);
			
	if (!MCMemoryResizeArray(t_enum -> element_count + 1, t_enum -> elements, t_enum -> element_count))
		return false;
		
	t_enum -> elements[t_enum -> element_count - 1] . where = p_where;
	t_enum -> elements[t_enum -> element_count - 1] . key = ValueRetain(p_element);
	t_enum -> elements[t_enum -> element_count - 1] . value = NumberGetInteger(p_value);
	
	return true;
}

bool InterfaceBeginHandler(InterfaceRef self, Position p_where, HandlerType p_type, NameRef p_name)
{
	MCLog("%s - %s handler %s", PositionDescribe(p_where), p_type == kHandlerTypeCommand || p_type == kHandlerTypeTailCommand ? "command" : "function", StringGetCStringPtr(NameGetString(p_name)));
	
	Handler *t_handler;
	t_handler = nil;
	for(uint32_t i = 0; i < self -> handler_count; i++)
		if (NameEqualCaseless(p_name, self -> handlers[i] . name))
		{
			t_handler = &self -> handlers[i];
			break;
		}
		
	// RULE: Variants of handlers must all have the same type.
	if (t_handler != nil && t_handler -> type != p_type)
		InterfaceReport(self, p_where, kInterfaceErrorCannotMixHandlerTypes, p_name);
		
	if (t_handler == nil)
	{
		if (!MCMemoryResizeArray(self -> handler_count + 1, self -> handlers, self -> handler_count))
			return false;
			
		t_handler = &self -> handlers[self -> handler_count - 1];
		t_handler -> type = p_type;
		t_handler -> name = ValueRetain(p_name);
	}
	
	if (!MCMemoryResizeArray(t_handler -> variant_count + 1, t_handler -> variants, t_handler -> variant_count))
		return false;
		
	t_handler -> variants[t_handler -> variant_count - 1] . where = p_where;
	
	self -> current_handler = t_handler;
	
	return true;
}

bool InterfaceEndHandler(InterfaceRef self)
{
	Handler *t_handler;
	t_handler = self -> current_handler;
	
	HandlerVariant *t_variant;
	t_variant = &t_handler -> variants[t_handler -> variant_count - 1];

	// If no binding specified, take the name of the handler
	if (t_variant -> binding == nil)
		t_variant -> binding = ValueRetain(t_handler -> name);
		
	// RULE: Target functions can be referenced only once
	uint32_t t_usage_count;
	t_usage_count = 0;
	if (NameEqual(t_variant -> binding, self -> startup_hook))
		t_usage_count += 1;
	if (NameEqual(t_variant -> binding, self -> shutdown_hook))
		t_usage_count += 1;
	for(uint32_t i = 0; i < self -> handler_count; i++)
		for(uint32_t j = 0; j < self -> handlers[i] . variant_count; j++)
			if (NameEqual(t_variant -> binding, self -> handlers[i] . variants[j] . binding))
				t_usage_count += 1;
	if (t_usage_count > 1)
		InterfaceReport(self, t_variant -> where, kInterfaceErrorTargetAlreadyUsed, t_variant -> binding);
	
	// RULE: Variants must have unique signatures
	for(uint32_t i = 0; i < t_handler -> variant_count - 1; i++)
	{
		if (t_variant == &t_handler -> variants[i])
			continue;
		if (t_variant -> minimum_parameter_count <= t_handler -> variants[i] . parameter_count &&
			t_variant -> parameter_count >= t_handler -> variants[i] . minimum_parameter_count)
		{
			InterfaceReport(self, t_variant -> where, kInterfaceErrorAmbiguousHandler, t_handler -> name);
			break;
		}
	}

	return true;
}

bool InterfaceDefineHandlerParameter(InterfaceRef self, Position p_where, ParameterType p_param_type, NameRef p_name, NameRef p_type, ValueRef p_default)
{	
	static const char *s_param_types[] = {"in", "out", "inout", "ref"};
	MCLog("%s - %s%s parameter %s as %s", PositionDescribe(p_where),
			p_default != nil ? "optional " : "",
			s_param_types[p_param_type],
			StringGetCStringPtr(NameGetString(p_name)), 
			StringGetCStringPtr(NameGetString(p_type)));
	
	HandlerVariant *t_variant;
	t_variant = &self -> current_handler -> variants[self -> current_handler -> variant_count - 1];
	
	// RULE: Type must be defined
	InterfaceCheckType(self, p_where, p_type);
	
	// RULE: 'ref' not currently supported
	if (p_param_type == kParameterTypeRef)
		InterfaceReport(self, p_where, kInterfaceErrorInvalidParameterType, nil);
	
	// RULE: optional 'boolean' not currently supported
	if (NameEqualToCString(p_type, "boolean") && p_default != nil)
		InterfaceReport(self, p_where, kInterfaceErrorNoOptionalBoolean, nil);
	
	// RULE: optional parameters can only be 'in'
	if (p_default != nil && p_param_type != kParameterTypeIn)
		InterfaceReport(self, p_where, kInterfaceErrorOptionalParamImpliesIn, nil);
	
	// RULE: Parameter name must be unique
	for(uint32_t i = 0; i < t_variant -> parameter_count; i++)
		if (NameEqualCaseless(p_name, t_variant -> parameters[i] . name))
		{
			InterfaceReport(self, p_where, kInterfaceErrorParamAlreadyDefined, p_name);
			break;
		}
		
	// RULE: No non-optional parameters after an optional one
	if (p_default == nil &&
		t_variant -> parameter_count > 0 &&
		t_variant -> parameters[t_variant -> parameter_count - 1] . default_value != nil)
		InterfaceReport(self, p_where, kInterfaceErrorParamAfterOptionalParam, nil);
	
	if (!MCMemoryResizeArray(t_variant -> parameter_count + 1, t_variant -> parameters, t_variant -> parameter_count))
		return false;
		
	t_variant -> parameters[t_variant -> parameter_count - 1] . where = p_where;
	t_variant -> parameters[t_variant -> parameter_count - 1] . mode = p_param_type;
	t_variant -> parameters[t_variant -> parameter_count - 1] . name = ValueRetain(p_name);
	t_variant -> parameters[t_variant -> parameter_count - 1] . type = ValueRetain(p_type);
	t_variant -> parameters[t_variant -> parameter_count - 1] . default_value = ValueRetain(p_default);
	
	if (p_default == nil)
		t_variant -> minimum_parameter_count += 1;
	
	return true;
}

bool InterfaceDefineHandlerReturn(InterfaceRef self, Position p_where, NameRef p_type, bool p_indirect)
{
	MCLog("%s - return %s%s", PositionDescribe(p_where),
			p_indirect ? "indirect " : "",
			StringGetCStringPtr(NameGetString(p_type)));
			
	HandlerVariant *t_variant;
	t_variant = &self -> current_handler -> variants[self -> current_handler -> variant_count - 1];

	InterfaceCheckType(self, p_where, p_type);
	
	t_variant -> return_type = ValueRetain(p_type);
	t_variant -> return_type_indirect = p_indirect;
	
	return true;
}

bool InterfaceDefineHandlerBinding(InterfaceRef self, Position p_where, NameRef p_name)
{
	MCLog("%s - call %s", PositionDescribe(p_where),
			StringGetCStringPtr(NameGetString(p_name)));
			
	self -> current_handler -> variants[self -> current_handler -> variant_count - 1] . binding = ValueRetain(p_name);
			
	return true;
}

////////////////////////////////////////////////////////////////////////////////
