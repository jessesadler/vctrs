% Generated by roxygen2: do not edit by hand
% Please edit documentation in R/size.R
\name{\%0\%}
\alias{\%0\%}
\title{Default value for empty vectors}
\usage{
x \%0\% y
}
\arguments{
\item{x}{A vector}

\item{y}{Value to use to \code{x} is empty. To preserve type-stability, should
be the same type as \code{x}.}
}
\description{
Use this inline operator when you need to provide a default value for
empty (as defined by \code{\link[=vec_is_empty]{vec_is_empty()}}) vectors.
}
\examples{
1:10 \%0\% 5
integer() \%0\% 5
}
