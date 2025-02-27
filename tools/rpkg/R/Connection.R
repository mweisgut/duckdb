#' @include Driver.R

NULL

duckdb_connection <- function(duckdb_driver, dbdir, debug) {
  dbdir <- path.expand(dbdir)
    if (debug) {
        message("CONNECT_START ", dbdir)
      }
  database_ref = .Call(duckdb_startup_R, dbdir)
  if (debug) {
        message("CONNECT_END ", dbdir)
      }
  new(
    "duckdb_connection",
    dbdir=dbdir,
    database_ref = database_ref,
    conn_ref = .Call(duckdb_connect_R, database_ref),
    driver = duckdb_driver,
    debug = debug
  )
}

#' @rdname DBI
#' @export
setClass(
  "duckdb_connection",
  contains = "DBIConnection",
  slots = list(dbdir= "character", database_ref = "externalptr", conn_ref = "externalptr", driver = "duckdb_driver", debug="logical")
)

#' @rdname DBI
#' @inheritParams methods::show
#' @export
setMethod("show", "duckdb_connection",
          function(object) {
            cat(sprintf("<duckdb_connection %s dbdir='%s' database_ref=%s>\n", extptr_str(object@conn_ref), object@dbdir, extptr_str(object@database_ref)))
          })

#' @rdname DBI
#' @inheritParams DBI::dbIsValid
#' @export
setMethod("dbIsValid", "duckdb_connection",
          function(dbObj, ...) {
            valid <- FALSE
            tryCatch ({
              dbExecute(dbObj, SQL("SELECT 1"))
              valid <- TRUE
            }, error = function(c) {
            })
            valid
          })

#' @rdname DBI
#' @inheritParams DBI::dbDisconnect
#' @export
setMethod("dbDisconnect", "duckdb_connection",
          function(conn, ...) {
            if (!dbIsValid(conn)) {
              warning("Connection already closed.", call. = FALSE)
            }
            .Call(duckdb_disconnect_R, conn@conn_ref)
            invisible(TRUE)
          })

#' @rdname DBI
#' @inheritParams DBI::dbSendQuery
#' @export
setMethod("dbSendQuery", c("duckdb_connection", "character"),
          function(conn, statement, ...) {
            if (conn@debug) {
              message("Q ", statement)
            }
		    statement <- enc2utf8(statement)
            resultset <- .Call(duckdb_query_R, conn@conn_ref, statement)
            attr(resultset, "row.names") <-
              c(NA_integer_, as.integer(-1 * length(resultset[[1]])))
            class(resultset) <- "data.frame"
            duckdb_result(
              connection = conn,
              statement = statement,
              has_resultset = TRUE,
              resultset = resultset
            )
          })

#' @rdname DBI
#' @inheritParams DBI::dbSendStatement
#' @export
setMethod("dbSendStatement", c("duckdb_connection", "character"),
          function(conn, statement, ...) {
            if (conn@debug) {
              message("S ", statement)
            }
		    statement <- enc2utf8(statement)
            resultset <- .Call(duckdb_query_R, conn@conn_ref, statement)
            duckdb_result(
              connection = conn,
              statement = statement,
              has_resultset = FALSE,
              rows_affected = as.numeric(resultset[[1]][1])
            )
          })

#' @rdname DBI
#' @inheritParams DBI::dbDataType
#' @export
setMethod("dbDataType", "duckdb_connection",
          function(dbObj, obj, ...) {
            dbDataType(dbObj@driver, obj, ...)
          })

check_flag <- function(x) {
  if (is.null(x) || is.na(x) || !is.logical(x) || length(x) != 1) {
    stop("flags need to be scalar logicals")
  }
}

#' @rdname DBI
#' @inheritParams DBI::dbWriteTable
#' @param overwrite Allow overwriting the destination table. Cannot be
#'   `TRUE` if `append` is also `TRUE`.
#' @param append Allow appending to the destination table. Cannot be
#'   `TRUE` if `overwrite` is also `TRUE`.
#' @export
setMethod("dbWriteTable", c("duckdb_connection", "character", "data.frame"),
          function(conn,
                   name,
                   value,
                   row.names = FALSE,
                   overwrite = FALSE,
                   append = FALSE,
                   field.types = NULL,
                   temporary = FALSE,
                   ...) {
            check_flag(overwrite)
            check_flag(append)
            check_flag(temporary)
            
            # TODO: start a transaction if one is not already running
            
            if (temporary) {
              stop("Temporary tables not supported yet")
            }
            
            if (overwrite && append) {
              stop("Setting both overwrite and append makes no sense")
            }
            
            # oof
            if (!is.null(field.types) &&
                (
                  !is.character(field.types) ||
                  any(is.na(names(field.types))) ||
                  length(unique(names(field.types))) != length(names(field.types)) ||
                  append
                )) {
              stop("invalid field.types argument")
            }
            value <- as.data.frame(value)
            if (!is.data.frame(value)) {
              stop("need a data frame as parameter")
            }
            
            # use Kirill's magic, convert rownames to additional column
            value <- sqlRownamesToColumn(value, row.names)
            
            if (dbExistsTable(conn, name)) {
              if (overwrite) {
                dbRemoveTable(conn, name)
              }
              if (!overwrite && !append) {
                stop(
                  "Table ",
                  name,
                  " already exists. Set overwrite=TRUE if you want
                  to remove the existing table. Set append=TRUE if you would like to add the new data to the
                  existing table."
                )
              }
              if (append && any(names(value) != dbListFields(conn, name))) {
                stop("Column name mismatch for append")
              }
              }
            
            if (!dbExistsTable(conn, name)) {
              table_name <- dbQuoteIdentifier(conn, name)
              column_names <- dbQuoteIdentifier(conn, names(value))
              column_types <-
                vapply(value, dbDataType, dbObj = conn, FUN.VALUE = "character")
              
              if (!is.null(field.types)) {
                mapped_column_types <- field.types[names(value)]
                if (any(is.na(mapped_column_types)) ||
                    length(mapped_column_types) != length(names(value))) {
                  stop("Column name/type mismatch")
                }
                column_types <- mapped_column_types
              }
              
              schema_str <- paste(column_names, column_types, collapse = ", ")
              dbExecute(conn, SQL(sprintf(
                "CREATE TABLE %s (%s)", table_name, schema_str
              )))
            }
			
			if (length(value[[1]])) {
				classes <- unlist(lapply(value, function(v){
				  class(v)[[1]]
				}))
				for (c in names(classes[classes=="character"])) {
				  value[[c]] <- enc2utf8(value[[c]])
				}
				for (c in names(classes[classes=="factor"])) {
				  levels(value[[c]]) <- enc2utf8(levels(value[[c]]))
				}
			}
            
            .Call(duckdb_append_R, conn@conn_ref, name, value)
            invisible(TRUE)
          })

#' @rdname DBI
#' @inheritParams DBI::dbListTables
#' @export
setMethod("dbListTables", "duckdb_connection",
          function(conn, ...) {
            dbGetQuery(conn,
                       SQL(
                         "SELECT name FROM sqlite_master() WHERE type='table' ORDER BY name"
                       ))[[1]]
          })

#' @rdname DBI
#' @inheritParams DBI::dbExistsTable
#' @export
setMethod("dbExistsTable", c("duckdb_connection", "character"),
          function(conn, name, ...) {
            if (!dbIsValid(conn)) {
              stop("Invalid connection")
            }
            if (length(name) != 1) {
              stop("Can only have a single name argument")
            }
            exists <- FALSE
            tryCatch ({
              dbGetQuery(conn,
                         sqlInterpolate(
                           conn,
                           "SELECT * FROM ? WHERE FALSE",
                           dbQuoteIdentifier(conn, name)
                         ))
              exists <- TRUE
            }, error = function(c) {
            })
            exists
          })

#' @rdname DBI
#' @inheritParams DBI::dbListFields
#' @export
setMethod("dbListFields", c("duckdb_connection", "character"),
          function(conn, name, ...) {
            names(dbGetQuery(
              conn,
              sqlInterpolate(
                conn,
                "SELECT * FROM ? WHERE FALSE",
                dbQuoteIdentifier(conn, name)
              )
            ))
          })

#' @rdname DBI
#' @inheritParams DBI::dbRemoveTable
#' @export
setMethod("dbRemoveTable", c("duckdb_connection", "character"),
          function(conn, name, ...) {
            dbExecute(conn,
                      sqlInterpolate(conn, "DROP TABLE ?", dbQuoteIdentifier(conn, name)))
            invisible(TRUE)
          })

#' @rdname DBI
#' @inheritParams DBI::dbGetInfo
#' @export
setMethod("dbGetInfo", "duckdb_connection",
          function(dbObj, ...) {
            list(
              dbname = dbObj@dbdir,
              db.version = NA,
              username = NA,
              host = NA,
              port = NA
            )
          })

#' @rdname DBI
#' @inheritParams DBI::dbBegin
#' @export
setMethod("dbBegin", "duckdb_connection",
          function(conn, ...) {
            dbExecute(conn, SQL("BEGIN TRANSACTION"))
            invisible(TRUE)
          })

#' @rdname DBI
#' @inheritParams DBI::dbCommit
#' @export
setMethod("dbCommit", "duckdb_connection",
          function(conn, ...) {
            dbExecute(conn, SQL("COMMIT"))
            invisible(TRUE)
          })

#' @rdname DBI
#' @inheritParams DBI::dbRollback
#' @export
setMethod("dbRollback", "duckdb_connection",
          function(conn, ...) {
            dbExecute(conn, SQL("ROLLBACK"))
            invisible(TRUE)
          })
