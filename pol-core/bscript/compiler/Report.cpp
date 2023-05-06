#include "Report.h"

#include "bscript/compiler/file/SourceLocation.h"
#include "clib/logfacility.h"

namespace Pol::Bscript::Compiler
{
Report::Report( bool display_warnings, bool display_errors )
    : display_warnings( display_warnings ),
      display_errors( display_errors ),
      errors( 0 ),
      warnings( 0 )
{
}

void Report::report_error( const SourceLocation& source_location, const char* msg )
{
  ++errors;
  try
  {
    ERROR_PRINT << source_location << ": error: " << msg;
  }
  catch ( ... )
  {
  }
}

void Report::report_warning( const SourceLocation& source_location, const char* msg )
{
  ++warnings;
  try
  {
    ERROR_PRINT << source_location << ": warning: " << msg;
  }
  catch ( ... )
  {
  }
}

unsigned Report::error_count() const
{
  return errors;
}

unsigned Report::warning_count() const
{
  return warnings;
}

void Report::reset()
{
  errors = warnings = 0;
}


}  // namespace Pol::Bscript::Compiler
