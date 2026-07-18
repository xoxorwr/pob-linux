-- Stub for lcurl.safe - provides minimal mock API for HTTP operations
-- Full HTTP functionality requires building lcurl from source

local M = {}

-- Option constants
M.OPT_HTTPHEADER = 10023
M.OPT_USERAGENT = 10018
M.OPT_ACCEPT_ENCODING = 10102
M.OPT_FOLLOWLOCATION = 52
M.OPT_POST = 47
M.OPT_POSTFIELDS = 10015
M.OPT_IPRESolve = 13
M.OPT_PROXY = 10004
M.OPT_SSL_VERIFYPEER = 64
M.OPT_SSL_VERIFYHOST = 81
M.INFO_RESPONSE_CODE = 2097154

function M.easy()
	local handle = {}
	local opts = {}

	function handle:setopt(opt, val)
		opts[opt] = val
	end

	function handle:setopt_url(url)
		opts.url = url
	end

	function handle:setopt_headerfunction(func)
		opts.headerfunction = func
	end

	function handle:setopt_writefunction(func)
		opts.writefunction = func
	end

	function handle:perform()
		return nil, { msg = function() return "lcurl stub: HTTP not available" end }
	end

	function handle:getinfo(info)
		if info == M.INFO_RESPONSE_CODE then
			return 0
		end
		return nil
	end

	function handle:close()
	end

	return handle
end

return M
