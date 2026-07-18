-- Stub for lua-utf8 - delegates to standard string library
-- For ASCII text this is sufficient; full UTF-8 requires the real lua-utf8 library

local utf8 = {}

-- Delegate most functions to the standard string library
utf8.len = function(s) return #s end
utf8.sub = string.sub
utf8.find = string.find
utf8.gmatch = string.gmatch
utf8.gsub = string.gsub
utf8.match = string.match
utf8.reverse = string.reverse
utf8.rep = string.rep
utf8.lower = string.lower
utf8.upper = string.upper
utf8.byte = string.byte
utf8.char = string.char
utf8.format = string.format
utf8.trim = function(s) return s:match("^%s*(.-)%s*$") end

-- utf8.next: iterate to next/previous character boundary
-- Simplified: treats each byte as a character (works for ASCII)
function utf8.next(s, pos, step)
	step = step or 1
	if step > 0 then
		if pos < 1 then pos = 1 end
		if pos > #s then return nil end
		return math.min(pos + step, #s + 1)
	else
		if pos > #s + 1 then pos = #s + 1 end
		if pos <= 1 then return nil end
		return math.max(pos + step, 1)
	end
end

-- utf8.codes: iterator over codepoints (simplified for ASCII)
function utf8.codes(s)
	local i = 0
	local len = #s
	return function()
		i = i + 1
		if i <= len then
			return i, string.byte(s, i)
		end
	end
end

-- metatable to allow utf8() to call string functions directly
setmetatable(utf8, {
	__index = string,
})

return utf8
